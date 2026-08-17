#pragma once

#include <chrono>
#include <memory>

#include <driver/gpio.h>
#include <nvs_flash.h>

#include <BatteryManager.hpp>
#include <KernelStatus.hpp>
#include <Log.hpp>
#include <NvsStore.hpp>
#include <Restart.hpp>
#include <Watchdog.hpp>
#include <drivers/LedDriver.hpp>
#include <mqtt/MqttDriver.hpp>
#include <mqtt/MqttRoot.hpp>

#include <devices/DeviceDefinition.hpp>
#include <NetworkConfig.hpp>

using namespace std::chrono;
using namespace cornucopia::ugly_duckling::devices;
using namespace cornucopia::ugly_duckling::kernel;

static void performFactoryReset(const std::shared_ptr<LedDriver>& statusLed, bool completeReset) {
    LOGI("Performing factory reset");

    statusLed->turnOn();
    Task::delay(1s);
    statusLed->turnOff();
    Task::delay(1s);
    statusLed->turnOn();

    LOGI(" - Deleting wifi NVS entries...");
    esp_wifi_restore();

    if (completeReset) {
        Task::delay(1s);
        statusLed->turnOff();
        Task::delay(1s);
        statusLed->turnOn();

        LOGI(" - Deleting all NVS config entries...");
        nvs_flash_erase();
    }

    LOGI(" - Restarting...");
    delayedRestart();
}

std::shared_ptr<BatteryDriver> initBattery(const std::shared_ptr<DeviceDefinition>& deviceDefinition, const std::shared_ptr<I2CManager>& i2c) {
    auto battery = deviceDefinition->createBatteryDriver(i2c);
    if (battery != nullptr) {
        // If the battery voltage is below the device's threshold, we should not boot yet.
        // This is to prevent the device from booting and immediately shutting down
        // due to the high current draw of the boot process.
        auto voltage = battery->getVoltage();
        if (voltage != 0 && voltage < battery->parameters.bootThreshold) {
            ESP_LOGW("battery", "Battery voltage too low (%d mV < %d mV), entering deep sleep\n",
                voltage, battery->parameters.bootThreshold);
            enterLowPowerDeepSleep();
        }
    }
    return battery;
}

void initNvsFlash() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

std::shared_ptr<Watchdog> initWatchdog(seconds timeout) {
    return std::make_shared<Watchdog>("watchdog", timeout, true, [](WatchdogState state) {
        if (state == WatchdogState::TimedOut) {
            LOGE("Watchdog timed out");
            esp_system_abort("Watchdog timed out");
        }
    });
}

std::shared_ptr<MqttRoot> initMqtt(const std::shared_ptr<ModuleStates>& states, const std::string& clientId, const std::shared_ptr<NetworkConfig>& networkConfig, StateSource& mqttReady) {
    // NetworkConfig inherits from MqttDriver::Config, so we can upcast
    auto mqttConfig = std::static_pointer_cast<MqttDriver::Config>(networkConfig);
    auto mqtt = std::make_shared<MqttDriver>(states->networkReady, mqttConfig, clientId, mqttReady);
    const std::string& location = networkConfig->location.get();
    return std::make_shared<MqttRoot>(mqtt, (location.empty() ? "" : location + "/") + "devices/ugly-duckling/" + networkConfig->instance.get());
}
