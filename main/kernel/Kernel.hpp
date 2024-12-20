#pragma once

#include <chrono>
#include <functional>
#include <optional>

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_mac.h>
#include <esp_system.h>

#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>

#include <kernel/FileSystem.hpp>
#include <kernel/I2CManager.hpp>
#include <kernel/PowerManager.hpp>
#include <kernel/StateManager.hpp>
#include <kernel/Watchdog.hpp>
#include <kernel/drivers/LedDriver.hpp>
#include <kernel/drivers/MdnsDriver.hpp>
#include <kernel/drivers/RtcDriver.hpp>
#include <kernel/drivers/SwitchManager.hpp>
#include <kernel/drivers/WiFiDriver.hpp>
#include <kernel/mqtt/MqttDriver.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace farmhub::kernel::drivers;
using namespace farmhub::kernel::mqtt;

namespace farmhub::kernel {

template <typename TConfiguration>
class Kernel;

static RTC_DATA_ATTR int bootCount = 0;

static constexpr const char* UPDATE_FILE = "/update.json";

// TODO Move this to a separate file
static const std::string& getMacAddress() {
    static std::string macAddress;
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
        : version(farmhubVersion)
        , deviceConfig(deviceConfig)
        , mqttConfig(mqttConfig)
        , statusLed(statusLed) {

        LOGI("Initializing FarmHub kernel version %s on %s instance '%s' with hostname '%s' and MAC address %s",
            version.c_str(),
            deviceConfig.model.get().c_str(),
            deviceConfig.instance.get().c_str(),
            deviceConfig.getHostname().c_str(),
            getMacAddress().c_str());

        // TODO Allocate less memory when FARMHUB_DEBUG is disabled
        Task::loop("status-update", 3072, [this](Task&) { updateState(); });

        httpUpdateResult = handleHttpUpdate();
        if (!httpUpdateResult.empty()) {
            LOGE("HTTP update failed because: %s",
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

    const std::string& getHttpUpdateResult() const {
        return httpUpdateResult;
    }

    void prepareUpdate(const std::string& url) {
        JsonDocument doc;
        doc["url"] = url;
        std::string content;
        serializeJson(doc, content);
        fs.writeAll(UPDATE_FILE, content);
    }

    void performFactoryReset(bool completeReset) {
        LOGI("Performing factory reset");

        statusLed.turnOn();
        Task::delay(1s);
        statusLed.turnOff();
        Task::delay(1s);
        statusLed.turnOn();

        if (completeReset) {
            Task::delay(1s);
            statusLed.turnOff();
            Task::delay(1s);
            statusLed.turnOn();

            LOGI(" - Deleting the file system...");
            FileSystem::format();
        }

        LOGI(" - Clearing NVS...");
        nvs_flash_erase();

        LOGI(" - Restarting...");
        esp_restart();
    }

    const std::string version;

    FileSystem& fs { FileSystem::get() };

private:
    enum class KernelState {
        BOOTING,
        NETWORK_CONNECTING,
        NETWORK_CONFIGURING,
        RTC_SYNCING,
        MQTT_CONNECTING,
        INIT_FINISHING,
        TRANSMITTING,
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
            newState = KernelState::TRANSMITTING;
        } else {
            newState = KernelState::IDLE;
        }

        if (newState != state) {
            LOGD("Kernel state changed from %d to %d",
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
                case KernelState::TRANSMITTING:
                    statusLed.turnOn();
                    break;
                case KernelState::IDLE:
                    statusLed.turnOff();
                    break;
            };
        }
        stateManager.awaitStateChange();
    }

    std::string handleHttpUpdate() {
        if (!fs.exists(UPDATE_FILE)) {
            return "";
        }

        // Don't sleep while we are performing the update
        PowerManagementLockGuard sleepLock(PowerManager::noLightSleep);

        auto contents = fs.readAll(UPDATE_FILE);
        if (!contents.has_value()) {
            return "Failed to read update file";
        }
        JsonDocument doc;
        auto error = deserializeJson(doc, contents.value());
        int deleteError = fs.remove(UPDATE_FILE);
        if (deleteError) {
            return "Failed to delete update file";
        }

        if (error) {
            return "Failed to parse update.json: " + std::string(error.c_str());
        }
        std::string url = doc["url"];
        if (url.length() == 0) {
            return "Command contains empty url";
        }

        LOGI("Updating from version %s via URL %s",
            farmhubVersion, url.c_str());

        LOGD("Waiting for network...");
        WiFiConnection connection(wifi, WiFiConnection::Mode::NoAwait);
        if (!connection.await(15s)) {
            return "Network not ready, aborting update";
        }

        esp_http_client_config_t httpConfig = {
            .url = url.c_str(),
            .event_handler = httpEventHandler,
            // Additional buffers to fit headers
            // Updating directly via GitHub's release links requires these
            .buffer_size = 4 * 1024,
            .buffer_size_tx = 12 * 1024,
            .user_data = this,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .keep_alive_enable = true,
        };
        esp_https_ota_config_t otaConfig = {
            .http_config = &httpConfig,
        };
        esp_err_t ret = esp_https_ota(&otaConfig);
        if (ret == ESP_OK) {
            LOGI("Update succeeded, rebooting in 5 seconds...");
            Task::delay(5s);
            esp_restart();
        } else {
            LOGE("Update failed (%s), continuing with regular boot",
                esp_err_to_name(ret));
            return std::string("Firmware upgrade failed: ") + esp_err_to_name(ret);
        }
    }    // namespace farmhub::kernel

    static esp_err_t httpEventHandler(esp_http_client_event_t* event) {
        switch (event->event_id) {
            case HTTP_EVENT_ERROR:
                LOGE("HTTP error, status code: %d",
                    esp_http_client_get_status_code(event->client));
                break;
            case HTTP_EVENT_ON_CONNECTED:
                LOGD("HTTP connected");
                break;
            case HTTP_EVENT_HEADERS_SENT:
                LOGV("HTTP headers sent");
                break;
            case HTTP_EVENT_ON_HEADER:
                LOGV("HTTP header: %s: %s", event->header_key, event->header_value);
                break;
            case HTTP_EVENT_ON_DATA:
                LOGD("HTTP data: %d bytes", event->data_len);
                break;
            case HTTP_EVENT_ON_FINISH:
                LOGD("HTTP finished");
                break;
            case HTTP_EVENT_DISCONNECTED:
                LOGD("HTTP disconnected");
                break;
            default:
                LOGW("Unknown HTTP event %d", event->event_id);
                break;
        }
        return ESP_OK;
    }

    TDeviceConfiguration& deviceConfig;

public:
    // TODO Make this configurable
    Watchdog watchdog { "watchdog", 5min, true, [](WatchdogState state) {
                           if (state == WatchdogState::TimedOut) {
                               LOGE("Watchdog timed out");
                               esp_system_abort("Watchdog timed out");
                           }
                       } };

    PowerManager powerManager { deviceConfig.sleepWhenIdle.get() };

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

    std::string httpUpdateResult;

public:
    MqttDriver mqtt { wifi, mdns, mqttConfig, deviceConfig.instance.get(), deviceConfig.sleepWhenIdle.get(), mqttReadyState };
    SwitchManager switches;
    I2CManager i2c;
};

}    // namespace farmhub::kernel
