#pragma once

#include <chrono>
#include <concepts>
#include <functional>
#include <optional>

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_system.h>

#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>

#include <devices/DeviceConfiguration.hpp>

#include <kernel/FileSystem.hpp>
#include <kernel/HttpUpdate.hpp>
#include <kernel/I2CManager.hpp>
#include <kernel/NetworkUtil.hpp>
#include <kernel/PowerManager.hpp>
#include <kernel/ShutdownManager.hpp>
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
using namespace farmhub::devices;
using namespace farmhub::kernel::drivers;
using namespace farmhub::kernel::mqtt;

namespace farmhub::kernel {

static RTC_DATA_ATTR int bootCount = 0;

class Kernel {
public:
    Kernel(
        std::shared_ptr<DeviceConfiguration> deviceConfig,
        std::shared_ptr<MqttDriver::Config> mqttConfig,
        std::shared_ptr<LedDriver> statusLed,
        std::shared_ptr<ShutdownManager> shutdownManager,
        std::shared_ptr<I2CManager> i2c,
        std::shared_ptr<WiFiDriver> wifi)
        : version(farmhubVersion)
        , statusLed(statusLed)
        , shutdownManager(shutdownManager)
        , powerManager(deviceConfig->sleepWhenIdle.get())
        , wifi(wifi)
        , mdns(wifi->getNetworkReady(), deviceConfig->getHostname(), "ugly-duckling", version, mdnsReadyState)
        , rtc(wifi->getNetworkReady(), mdns, deviceConfig->ntp.get(), rtcInSyncState)
        , mqtt(std::make_shared<MqttDriver>(wifi->getNetworkReady(), mdns, mqttConfig, deviceConfig->instance.get(), deviceConfig->sleepWhenIdle.get(), mqttReadyState))
        , i2c(i2c) {

        LOGI("Initializing FarmHub kernel version %s on %s instance '%s' with hostname '%s' and MAC address %s",
            version.c_str(),
            deviceConfig->model.get().c_str(),
            deviceConfig->instance.get().c_str(),
            deviceConfig->getHostname().c_str(),
            getMacAddress().c_str());

        // TODO Allocate less memory when FARMHUB_DEBUG is disabled
        Task::loop("status-update", 3072, [this](Task&) { updateState(); });

        httpUpdateResult = handleHttpUpdate(fs, wifi);
        if (!httpUpdateResult.empty()) {
            LOGE("HTTP update failed because: %s",
                httpUpdateResult.c_str());
        }
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

        statusLed->turnOn();
        Task::delay(1s);
        statusLed->turnOff();
        Task::delay(1s);
        statusLed->turnOn();

        if (completeReset) {
            Task::delay(1s);
            statusLed->turnOff();
            Task::delay(1s);
            statusLed->turnOn();

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
        if (wifi->getConfigPortalRunning().isSet()) {
            // We are waiting for the user to configure the network
            newState = KernelState::NETWORK_CONFIGURING;
        } else if (wifi->getNetworkConnecting().isSet()) {
            // We are waiting for network connection
            newState = KernelState::NETWORK_CONNECTING;
        } else if (!rtcInSyncState.isSet()) {
            newState = KernelState::RTC_SYNCING;
        } else if (!mqttReadyState.isSet()) {
            // We are waiting for MQTT connection
            newState = KernelState::MQTT_CONNECTING;
        } else if (!kernelReadyState.isSet()) {
            // We are waiting for init to finish
            newState = KernelState::INIT_FINISHING;
        } else if (wifi->getNetworkReady().isSet()) {
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
                    statusLed->turnOff();
                    break;
                case KernelState::NETWORK_CONNECTING:
                    statusLed->blink(200ms);
                    break;
                case KernelState::NETWORK_CONFIGURING:
                    statusLed->blinkPattern({ 100ms, -100ms, 100ms, -100ms, 100ms, -500ms });
                    break;
                case KernelState::RTC_SYNCING:
                    statusLed->blink(500ms);
                    break;
                case KernelState::MQTT_CONNECTING:
                    statusLed->blink(1000ms);
                    break;
                case KernelState::INIT_FINISHING:
                    statusLed->blink(1500ms);
                    break;
                case KernelState::TRANSMITTING:
                    statusLed->turnOff();
                    break;
                case KernelState::IDLE:
                    statusLed->turnOff();
                    break;
            };
        }
        stateManager.awaitStateChange();
    }

    const std::shared_ptr<LedDriver> statusLed;

public:
    const std::shared_ptr<ShutdownManager> shutdownManager;

    // TODO Make this configurable
    Watchdog watchdog { "watchdog", 5min, true, [](WatchdogState state) {
                           if (state == WatchdogState::TimedOut) {
                               LOGE("Watchdog timed out");
                               esp_system_abort("Watchdog timed out");
                           }
                       } };

    PowerManager powerManager;

private:
    KernelState state = KernelState::BOOTING;
    StateManager stateManager;
    StateSource rtcInSyncState = stateManager.createStateSource("rtc-in-sync");
    StateSource mdnsReadyState = stateManager.createStateSource("mdns-ready");
    StateSource mqttReadyState = stateManager.createStateSource("mqtt-ready");
    StateSource kernelReadyState = stateManager.createStateSource("kernel-ready");

public:
    const std::shared_ptr<WiFiDriver> wifi;

private:
    MdnsDriver mdns;
    RtcDriver rtc;

    std::string httpUpdateResult;

public:
    const std::shared_ptr<MqttDriver> mqtt;
    const std::shared_ptr<SwitchManager> switches = std::make_shared<SwitchManager>();
    const std::shared_ptr<I2CManager> i2c;
};

}    // namespace farmhub::kernel
