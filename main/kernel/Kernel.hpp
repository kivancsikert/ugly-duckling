#pragma once

#include <chrono>
#include <concepts>
#include <functional>
#include <optional>

#include <esp_system.h>

#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>

#include <devices/DeviceConfiguration.hpp>

#include <kernel/I2CManager.hpp>
#include <kernel/NetworkUtil.hpp>
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
using namespace farmhub::devices;
using namespace farmhub::kernel::drivers;
using namespace farmhub::kernel::mqtt;

namespace farmhub::kernel {

static RTC_DATA_ATTR int bootCount = 0;

class Kernel {
public:
    Kernel(
        std::shared_ptr<LedDriver> statusLed,
        std::shared_ptr<WiFiDriver> wifi,
        std::shared_ptr<MdnsDriver> mdns,
        std::shared_ptr<RtcDriver> rtc,
        std::shared_ptr<MqttDriver> mqtt)
        : statusLed(statusLed)
        , wifi(wifi)
        , mdns(mdns)
        , rtc(rtc)
        , mqtt(mqtt) {
        // TODO Allocate less memory when FARMHUB_DEBUG is disabled
        Task::loop("status-update", 3072, [this](Task&) { updateState(); });
    }

    const State& getRtcInSyncState() const {
        return rtc->getInSync();
    }

    const StateSource& getKernelReadyState() {
        return kernelReadyState;
    }

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
        } else if (!rtc->getInSync().isSet()) {
            newState = KernelState::RTC_SYNCING;
        } else if (!mqtt->getReady().isSet()) {
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

private:
    KernelState state = KernelState::BOOTING;
    StateManager stateManager;
    StateSource kernelReadyState = stateManager.createStateSource("kernel-ready");

public:
    const std::shared_ptr<WiFiDriver> wifi;

private:
    const std::shared_ptr<MdnsDriver> mdns;
    const std::shared_ptr<RtcDriver> rtc;
    const std::shared_ptr<MqttDriver> mqtt;
};

}    // namespace farmhub::kernel
