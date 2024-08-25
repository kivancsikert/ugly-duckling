#pragma once

#include <chrono>
#include <functional>
#include <optional>

#include <HTTPUpdate.h>

#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>

#include <kernel/FileSystem.hpp>
#include <kernel/I2CManager.hpp>
#include <kernel/Log.hpp>
#include <kernel/SleepManager.hpp>
#include <kernel/drivers/LedDriver.hpp>
#include <kernel/drivers/MdnsDriver.hpp>
#include <kernel/drivers/MqttDriver.hpp>
#include <kernel/drivers/RtcDriver.hpp>
#include <kernel/drivers/SwitchManager.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

#include <version.h>

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace farmhub::kernel::drivers;

namespace farmhub::kernel {

template <typename TConfiguration>
class Kernel;

static RTC_DATA_ATTR int bootCount = 0;

static const String UPDATE_FILE = "/update.json";

// TODO Move this to a separate file
static const String& getMacAddress() {
    static String macAddress;
    if (macAddress.length() == 0) {
        uint8_t rawMac[6];
        for (int i = 0; i < 6; i++) {
            rawMac[i] = 0;
        }
        if (esp_efuse_mac_get_default(rawMac) != ESP_OK) {
            macAddress = "??:??:??:??:??:??:??:??";
        } else {
            char mac[24];
            sprintf(mac, "%02x:%02x:%02x:%02x:%02x:%02x",
                rawMac[0], rawMac[1], rawMac[2], rawMac[3],
                rawMac[4], rawMac[5]);
            macAddress = mac;
        }
    }
    return macAddress;
}

template <typename TDeviceConfiguration>
class Kernel {
public:
    Kernel(TDeviceConfiguration& deviceConfig, MqttDriver::Config& mqttConfig, LedDriver& statusLed)
        : version(VERSION)
        , deviceConfig(deviceConfig)
        , mqttConfig(mqttConfig)
        , statusLed(statusLed) {

        Log.info("Initializing FarmHub kernel version %s on %s instance '%s' with hostname '%s'",
            version.c_str(),
            deviceConfig.model.get().c_str(),
            deviceConfig.instance.get().c_str(),
            deviceConfig.getHostname().c_str());

        // TODO Allocate less memory when FARMHUB_DEBUG is disabled
        Task::loop("status-update", 2560, [this](Task&) { updateState(); });

        httpUpdateResult = handleHttpUpdate();
    }

    const State& getNetworkReadyState() const {
        return networkReadyState;
    }

    const State& getRtcInSyncState() const {
        return rtcInSyncState;
    }

    const StateSource& getKernelReadyState() {
        return kernelReadyState;
    }

    const String& getHttpUpdateResult() const {
        return httpUpdateResult;
    }

    void prepareUpdate(const String& url) {
        auto fUpdate = fs.open(UPDATE_FILE, FILE_WRITE);
        JsonDocument doc;
        doc["url"] = url;
        serializeJson(doc, fUpdate);
        fUpdate.close();
    }

    void performFactoryReset() {
        Log.printlnToSerial("Performing factory reset");
        Log.flushSerial();

        statusLed.turnOn();
        delay(1000);
        statusLed.turnOff();
        delay(1000);
        statusLed.turnOn();

        Log.printlnToSerial(" - Deleting the file system...");
        Log.flushSerial();
        fs.reset();

        Log.printlnToSerial(" - Clearing NVS...");
        Log.flushSerial();

        nvs_flash_erase();

        Log.printlnToSerial(" - Restarting...");
        Log.flushSerial();

        ESP.restart();
    }

    const String version;

    FileSystem& fs { FileSystem::get() };

private:
    enum class KernelState {
        BOOTING,
        NETWORK_CONNECTING,
        NETWORK_CONFIGURING,
        RTC_SYNCING,
        MQTT_CONNECTING,
        INIT_FINISHING,
        READY
    };

    void updateState() {
        KernelState newState;
        if (!networkReadyState.isSet()) {
            // We don't have network
            if (configPortalRunningState.isSet()) {
                // We are waiting for the user to configure the network
                newState = KernelState::NETWORK_CONFIGURING;
            } else {
                // We are waiting for network connection
                newState = KernelState::NETWORK_CONNECTING;
            }
        } else if (!rtcInSyncState.isSet()) {
            newState = KernelState::RTC_SYNCING;
        } else if (!mqttReadyState.isSet()) {
            // We are waiting for MQTT connection
            newState = KernelState::MQTT_CONNECTING;
        } else if (!kernelReadyState.isSet()) {
            // We are waiting for init to finish
            newState = KernelState::INIT_FINISHING;
        } else {
            newState = KernelState::READY;
        }

        if (newState != state) {
            Log.debug("Kernel state changed from %d to %d",
                static_cast<int>(state), static_cast<int>(newState));
            state = newState;
            switch (newState) {
                case KernelState::BOOTING:
                    statusLed.turnOff();
                    break;
                case KernelState::NETWORK_CONNECTING:
                    statusLed.blink(200ms);
                    break;
                case KernelState::NETWORK_CONFIGURING:
                    statusLed.blinkPattern({ 100ms, -100ms, 100ms, -100ms, 100ms, -500ms });
                    break;
                case KernelState::RTC_SYNCING:
                    statusLed.blink(500ms);
                    break;
                case KernelState::MQTT_CONNECTING:
                    statusLed.blink(1000ms);
                    break;
                case KernelState::INIT_FINISHING:
                    statusLed.blink(1500ms);
                    break;
                case KernelState::READY:
                    if (deviceConfig.sleepWhenIdle.get()) {
                        statusLed.turnOff();
                    } else {
                        statusLed.turnOn();
                    }
                    break;
            };
        }
        stateManager.awaitStateChange();
    }

    String handleHttpUpdate() {
        if (!fs.exists(UPDATE_FILE)) {
            return "";
        }

        Log.info("Starting update...");
        auto fUpdate = fs.open(UPDATE_FILE, FILE_READ);
        JsonDocument doc;
        auto error = deserializeJson(doc, fUpdate);
        fUpdate.close();
        fs.remove(UPDATE_FILE);

        if (error) {
            return "Failed to parse update.json: " + String(error.c_str());
        }
        String url = doc["url"];
        if (url.length() == 0) {
            return "Command contains empty url";
        }

        Log.debug("Waiting for network...");
        if (!networkReadyState.awaitSet(1min)) {
            return "Network not ready, aborting update";
        }

        httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        Log.info("Updating from version %s via URL %s",
            VERSION, url.c_str());

        HTTPUpdateResult result = HTTP_UPDATE_NO_UPDATES;
        // Run in separate task to allocate enough stack
        SemaphoreHandle_t completionSemaphore = xSemaphoreCreateBinary();
        Task::run("update", 8192, [&](Task& task) {
            // Allocate on heap to avoid wasting stack
            std::unique_ptr<WiFiClientSecure> client = std::make_unique<WiFiClientSecure>();
            // Allow insecure connections for testing
            client->setInsecure();
            result = httpUpdate.update(*client, url, VERSION);
            xSemaphoreGive(completionSemaphore);
        });
        xSemaphoreTake(completionSemaphore, portMAX_DELAY);

        switch (result) {
            case HTTP_UPDATE_FAILED:
                return httpUpdate.getLastErrorString() + " (" + String(httpUpdate.getLastError()) + ")";
            case HTTP_UPDATE_NO_UPDATES:
                return "No updates available";
            case HTTP_UPDATE_OK:
                return "Update OK";
            default:
                return "Unknown response";
        }
    }

    TDeviceConfiguration& deviceConfig;

public:
    SleepManager sleepManager { deviceConfig.sleepWhenIdle.get() };

private:
    MqttDriver::Config& mqttConfig;

    LedDriver& statusLed;
    KernelState state = KernelState::BOOTING;
    StateManager stateManager;
    StateSource networkReadyState = stateManager.createStateSource("network-ready");
    StateSource configPortalRunningState = stateManager.createStateSource("config-portal-running");
    StateSource rtcInSyncState = stateManager.createStateSource("rtc-in-sync");
    StateSource mdnsReadyState = stateManager.createStateSource("mdns-ready");
    StateSource mqttReadyState = stateManager.createStateSource("mqtt-ready");
    StateSource kernelReadyState = stateManager.createStateSource("kernel-ready");

    WiFiDriver wifi { networkReadyState, configPortalRunningState, deviceConfig.getHostname(), deviceConfig.sleepWhenIdle.get() };
    MdnsDriver mdns { networkReadyState, deviceConfig.getHostname(), "ugly-duckling", version, mdnsReadyState };
    RtcDriver rtc { networkReadyState, mdns, deviceConfig.ntp.get(), rtcInSyncState };

    String httpUpdateResult;

public:
    MqttDriver mqtt { networkReadyState, mdns, mqttConfig, deviceConfig.instance.get(), mqttReadyState };
    SwitchManager switches;
    I2CManager i2c;
};

}    // namespace farmhub::kernel
