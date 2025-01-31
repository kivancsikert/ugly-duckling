#pragma once

#include <chrono>
#include <memory>

#include <esp_netif.h>
#include <esp_pm.h>
#include <esp_wifi.h>

#include <kernel/BootClock.hpp>
#include <kernel/Command.hpp>
#include <kernel/Concurrent.hpp>
#include <kernel/CrashManager.hpp>
#include <kernel/DebugConsole.hpp>
#include <kernel/Strings.hpp>
#include <kernel/Task.hpp>
#include <kernel/mqtt/MqttDriver.hpp>
#include <kernel/mqtt/MqttRoot.hpp>
#include <kernel/mqtt/MqttTelemetryPublisher.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::kernel::mqtt;

#if defined(MK4)
#include <devices/UglyDucklingMk4.hpp>
typedef farmhub::devices::UglyDucklingMk4 TDeviceDefinition;
typedef farmhub::devices::Mk4Config TDeviceConfiguration;
#elif defined(MK5)
#include <devices/UglyDucklingMk5.hpp>
typedef farmhub::devices::UglyDucklingMk5 TDeviceDefinition;
typedef farmhub::devices::Mk5Config TDeviceConfiguration;
#elif defined(MK6)
#include <devices/UglyDucklingMk6.hpp>
typedef farmhub::devices::UglyDucklingMk6 TDeviceDefinition;
typedef farmhub::devices::Mk6Config TDeviceConfiguration;
#elif defined(MK7)
#include <devices/UglyDucklingMk7.hpp>
typedef farmhub::devices::UglyDucklingMk7 TDeviceDefinition;
typedef farmhub::devices::Mk7Config TDeviceConfiguration;
#elif defined(MK8)
#include <devices/UglyDucklingMk8.hpp>
typedef farmhub::devices::UglyDucklingMk8 TDeviceDefinition;
typedef farmhub::devices::Mk8Config TDeviceConfiguration;
#else
#error "No device defined"
#endif

using namespace farmhub::kernel;

namespace farmhub::devices {

class MemoryTelemetryProvider : public TelemetryProvider {
public:
    void populateTelemetry(JsonObject& json) override {
        json["free-heap"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        json["min-heap"] = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    }
};

class WiFiTelemetryProvider : public TelemetryProvider {
public:
    WiFiTelemetryProvider(const std::shared_ptr<WiFiDriver> wifi)
        : wifi(wifi) {
    }

    void populateTelemetry(JsonObject& json) override {
        json["uptime"] = wifi->getUptime().count();
    }

private:
    const std::shared_ptr<WiFiDriver> wifi;
};

class PowerManagementTelemetryProvider : public TelemetryProvider {
public:
    PowerManagementTelemetryProvider(std::shared_ptr<PowerManager> powerManager)
        : powerManager(powerManager) {
    }

    void populateTelemetry(JsonObject& json) override {
#ifdef CONFIG_PM_LIGHT_SLEEP_CALLBACKS
        json["sleep-time"] = powerManager->getLightSleepTime().count();
        json["sleep-count"] = powerManager->getLightSleepCount();
#endif
    }

private:
    const std::shared_ptr<PowerManager> powerManager;
};

class Device {
public:
    Device(
        const std::shared_ptr<TDeviceConfiguration> deviceConfig,
        const std::shared_ptr<TDeviceDefinition> deviceDefinition,
        const std::shared_ptr<FileSystem> fs,
        const std::shared_ptr<WiFiDriver> wifi,
        const std::shared_ptr<BatteryManager> battery,
        const std::shared_ptr<Watchdog> watchdog,
        const std::shared_ptr<PowerManager> powerManager,
        const std::shared_ptr<MqttRoot> mqttDeviceRoot,
        const std::shared_ptr<PeripheralManager> peripheralManager,
        const std::shared_ptr<TelemetryPublisher> deviceTelemetryPublisher,
        const State& rtcInSync)
        : deviceDefinition(deviceDefinition)
        , fs(fs)
        , mqttDeviceRoot(mqttDeviceRoot) {
        mqttDeviceRoot->registerCommand(echoCommand);
        mqttDeviceRoot->registerCommand(pingCommand);
        // TODO Add reset-wifi command
        // mqttDeviceRoot->registerCommand(resetWifiCommand);
        mqttDeviceRoot->registerCommand(restartCommand);
        mqttDeviceRoot->registerCommand(sleepCommand);
        mqttDeviceRoot->registerCommand(fileListCommand);
        mqttDeviceRoot->registerCommand(fileReadCommand);
        mqttDeviceRoot->registerCommand(fileWriteCommand);
        mqttDeviceRoot->registerCommand(fileRemoveCommand);
        mqttDeviceRoot->registerCommand(httpUpdateCommand);

        // We want RTC to be in sync before we start setting up peripherals
        rtcInSync.awaitSet();

        JsonDocument peripheralsInitDoc;
        JsonArray peripheralsInitJson = peripheralsInitDoc.to<JsonArray>();

        auto builtInPeripheralsConfig = deviceDefinition->getBuiltInPeripherals();
        LOGD("Loading configuration for %d built-in peripherals",
            builtInPeripheralsConfig.size());
        for (auto& peripheralConfig : builtInPeripheralsConfig) {
            peripheralManager->createPeripheral(peripheralConfig, peripheralsInitJson);
        }

        auto& peripheralsConfig = deviceConfig->peripherals.get();
        LOGI("Loading configuration for %d user-configured peripherals",
            peripheralsConfig.size());
        bool peripheralError = false;
        for (auto& peripheralConfig : peripheralsConfig) {
            if (!peripheralManager->createPeripheral(peripheralConfig.get(), peripheralsInitJson)) {
                peripheralError = true;
            }
        }

        InitState initState = peripheralError ? InitState::PeripheralError : InitState::Success;

        mqttDeviceRoot->publish(
            "init",
            [&](JsonObject& json) {
                // TODO Remove redundant mentions of "ugly-duckling"
                json["type"] = "ugly-duckling";
                json["model"] = deviceConfig->model.get();
                json["id"] = deviceConfig->id.get();
                json["instance"] = deviceConfig->instance.get();
                json["mac"] = getMacAddress();
                auto device = json["deviceConfig"].to<JsonObject>();
                deviceConfig->store(device, false);
                // TODO Remove redundant mentions of "ugly-duckling"
                json["app"] = "ugly-duckling";
                json["version"] = farmhubVersion;
                json["reset"] = esp_reset_reason();
                json["wakeup"] = esp_sleep_get_wakeup_cause();
                json["bootCount"] = bootCount++;
                json["time"] = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
                json["state"] = static_cast<int>(initState);
                json["peripherals"].to<JsonArray>().set(peripheralsInitJson);
                json["sleepWhenIdle"] = powerManager->sleepWhenIdle;

                CrashManager::reportPreviousCrashIfAny(json);
            },
            Retention::NoRetain, QoS::AtLeastOnce, 5s);

        auto publishInterval = deviceConfig->publishInterval.get();
        Task::loop("telemetry", 8192, [this, publishInterval, watchdog, peripheralManager, deviceTelemetryPublisher](Task& task) {
            task.markWakeTime();

            deviceTelemetryPublisher->publishTelemetry();
            peripheralManager->publishTelemetry();

            // Signal that we are still alive
            watchdog->restart();

            // We always wait at least this much between telemetry updates
            const auto debounceInterval = 500ms;
            // Delay without updating last wake time
            task.delay(task.ticksUntil(debounceInterval));

            // Allow other tasks to trigger telemetry updates
            auto timeout = task.ticksUntil(publishInterval - debounceInterval);
            telemetryPublishQueue.pollIn(timeout);
        });
    }

private:
    enum class InitState {
        Success = 0,
        PeripheralError = 1,
    };

    const std::shared_ptr<TDeviceDefinition> deviceDefinition;
    const std::shared_ptr<FileSystem> fs;
    const std::shared_ptr<MqttRoot> mqttDeviceRoot;

    PingCommand pingCommand { [this]() {
        telemetryPublishQueue.offer(true);
    } };

    EchoCommand echoCommand;
    RestartCommand restartCommand;
    SleepCommand sleepCommand;
    FileListCommand fileListCommand { fs };
    FileReadCommand fileReadCommand { fs };
    FileWriteCommand fileWriteCommand { fs };
    FileRemoveCommand fileRemoveCommand { fs };
    HttpUpdateCommand httpUpdateCommand { [this](const std::string& url) {
        JsonDocument doc;
        doc["url"] = url;
        std::string content;
        serializeJson(doc, content);
        fs->writeAll(UPDATE_FILE, content);
    } };

    CopyQueue<bool> telemetryPublishQueue { "telemetry-publish", 1 };
};

}    // namespace farmhub::devices
