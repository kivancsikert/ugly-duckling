#pragma once

#include <chrono>
#include <functional>
#include <optional>

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_mac.h>

#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>

#include <kernel/FileSystem.hpp>
#include <kernel/I2CManager.hpp>
#include <kernel/Log.hpp>
#include <kernel/SleepManager.hpp>
#include <kernel/StateManager.hpp>
#include <kernel/drivers/LedDriver.hpp>
#include <kernel/drivers/MdnsDriver.hpp>
#include <kernel/drivers/MqttDriver.hpp>
#include <kernel/drivers/RtcDriver.hpp>
#include <kernel/drivers/SwitchManager.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

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
        if (esp_read_mac(rawMac, ESP_MAC_WIFI_STA) != ESP_OK) {
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
        : version(FARMHUB_VERSION)
        , deviceConfig(deviceConfig)
        , mqttConfig(mqttConfig)
        , statusLed(statusLed) {

        Log.info("Initializing FarmHub kernel version %s on %s instance '%s' with hostname '%s' and MAC address %s",
            version.c_str(),
            deviceConfig.model.get().c_str(),
            deviceConfig.instance.get().c_str(),
            deviceConfig.getHostname().c_str(),
            getMacAddress().c_str());

        // TODO Allocate less memory when FARMHUB_DEBUG is disabled
        Task::loop("status-update", 3072, [this](Task&) { updateState(); });

        httpUpdateResult = handleHttpUpdate();
        if (!httpUpdateResult.isEmpty()) {
            Log.error("HTTP update failed because: %s",
                httpUpdateResult.c_str());
        }
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

    void performFactoryReset(bool completeReset) {
        Log.printlnToSerial("Performing factory reset");

        statusLed.turnOn();
        delay(1000);
        statusLed.turnOff();
        delay(1000);
        statusLed.turnOn();

        if (completeReset) {
            delay(1000);
            statusLed.turnOff();
            delay(1000);
            statusLed.turnOn();

            Log.printlnToSerial(" - Deleting the file system...");
            fs.reset();
        }

        Log.printlnToSerial(" - Clearing NVS...");
        nvs_flash_erase();

        Log.printlnToSerial(" - Restarting...");
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
        TRNASMITTING,
        IDLE
    };

    void updateState() {
        KernelState newState;
        if (configPortalRunningState.isSet()) {
            // We are waiting for the user to configure the network
            newState = KernelState::NETWORK_CONFIGURING;
        } else if (networkConnectingState.isSet()) {
            // We are waiting for network connection
            newState = KernelState::NETWORK_CONNECTING;
        } else if (networkRequestedState.isSet() && !rtcInSyncState.isSet()) {
            newState = KernelState::RTC_SYNCING;
        } else if (networkRequestedState.isSet() && !mqttReadyState.isSet()) {
            // We are waiting for MQTT connection
            newState = KernelState::MQTT_CONNECTING;
        } else if (!kernelReadyState.isSet()) {
            // We are waiting for init to finish
            newState = KernelState::INIT_FINISHING;
        } else if (networkReadyState.isSet()) {
            newState = KernelState::TRNASMITTING;
        } else {
            newState = KernelState::IDLE;
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
                case KernelState::TRNASMITTING:
                    statusLed.turnOn();
                    break;
                case KernelState::IDLE:
                    statusLed.turnOff();
                    break;
            };
        }
        stateManager.awaitStateChange();
    }

    String handleHttpUpdate() {
        if (!fs.exists(UPDATE_FILE)) {
            return "";
        }

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

        Log.info("Updating from version %s via URL %s",
            FARMHUB_VERSION, url.c_str());

        Log.debug("Waiting for network...");
        WiFiConnection connection(wifi, WiFiConnection::Mode::NoAwait);
        if (!connection.await(15s)) {
            return "Network not ready, aborting update";
        }

        // TODO Disable power save mode for WiFi

        esp_http_client_config_t httpConfig = {
            .url = url.c_str(),
            .timeout_ms = duration_cast<milliseconds>(2min).count(),
            .max_redirection_count = 5,
            .event_handler = httpEventHandler,
            .buffer_size = 8192,
            .buffer_size_tx = 8192,
            .user_data = this,
            // TODO Do we need this?
            .skip_cert_common_name_check = true,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .keep_alive_enable = true,
        };
        esp_https_ota_config_t otaConfig = {
            .http_config = &httpConfig,
            // TODO Make it work without partial downloads
            // With this we seem to be able to install a new firmware, even if very slowly;
            // without it we get the error upon download completion: 'no more processes'.
            .partial_http_download = true,
        };
        esp_err_t ret = esp_https_ota(&otaConfig);
        if (ret == ESP_OK) {
            Log.info("Update succeeded, rebooting in 5 seconds...");
            Task::delay(5s);
            esp_restart();
        } else {
            Log.error("Update failed (err = %d), continuing with regular boot",
                ret);
            return "Firmware upgrade failed: " + String(ret);
        }
    }    // namespace farmhub::kernel

    static esp_err_t httpEventHandler(esp_http_client_event_t* event) {
        switch (event->event_id) {
            case HTTP_EVENT_ERROR:
                Log.error("HTTP error, status code: %d",
                    esp_http_client_get_status_code(event->client));
                break;
            case HTTP_EVENT_ON_CONNECTED:
                Log.debug("HTTP connected");
                break;
            case HTTP_EVENT_HEADERS_SENT:
                Log.trace("HTTP headers sent");
                break;
            case HTTP_EVENT_ON_HEADER:
                Log.trace("HTTP header: %s: %s", event->header_key, event->header_value);
                break;
            case HTTP_EVENT_ON_DATA:
                Log.debug("HTTP data: %d bytes", event->data_len);
                break;
            case HTTP_EVENT_ON_FINISH:
                Log.debug("HTTP finished");
                break;
            case HTTP_EVENT_DISCONNECTED:
                Log.debug("HTTP disconnected");
                break;
            default:
                Log.warn("Unknown HTTP event %d", event->event_id);
                break;
        }
        return ESP_OK;
    }

    TDeviceConfiguration& deviceConfig;

public:
    SleepManager sleepManager { deviceConfig.sleepWhenIdle.get() };

private:
    MqttDriver::Config& mqttConfig;

    LedDriver& statusLed;
    KernelState state = KernelState::BOOTING;
    StateManager stateManager;
    StateSource networkRequestedState = stateManager.createStateSource("network-requested");
    StateSource networkConnectingState = stateManager.createStateSource("network-connecting");
    StateSource networkReadyState = stateManager.createStateSource("network-ready");
    StateSource configPortalRunningState = stateManager.createStateSource("config-portal-running");
    StateSource rtcInSyncState = stateManager.createStateSource("rtc-in-sync");
    StateSource mdnsReadyState = stateManager.createStateSource("mdns-ready");
    StateSource mqttReadyState = stateManager.createStateSource("mqtt-ready");
    StateSource kernelReadyState = stateManager.createStateSource("kernel-ready");

public:
    WiFiDriver wifi { networkRequestedState, networkConnectingState, networkReadyState, configPortalRunningState, deviceConfig.getHostname(), deviceConfig.sleepWhenIdle.get() };

private:
    MdnsDriver mdns { wifi, deviceConfig.getHostname(), "ugly-duckling", version, mdnsReadyState };
    RtcDriver rtc { wifi, mdns, deviceConfig.ntp.get(), rtcInSyncState };

    String httpUpdateResult;

public:
    MqttDriver mqtt { wifi, mdns, mqttConfig, deviceConfig.instance.get(), deviceConfig.sleepWhenIdle.get(), mqttReadyState };
    SwitchManager switches;
    I2CManager i2c;
};

}    // namespace farmhub::kernel
