#pragma once

#include <chrono>
#include <memory>

#include <esp_core_dump.h>
#include <esp_netif.h>
#include <esp_pm.h>
#include <esp_private/esp_clk.h>
#include <esp_wifi.h>
#include <mbedtls/base64.h>

#include <kernel/BootClock.hpp>
#include <kernel/Command.hpp>
#include <kernel/Concurrent.hpp>
#include <kernel/DebugConsole.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/Strings.hpp>
#include <kernel/Task.hpp>
#include <kernel/mqtt/MqttDriver.hpp>
#include <kernel/mqtt/MqttRoot.hpp>

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

class MqttTelemetryPublisher : public TelemetryPublisher {
public:
    MqttTelemetryPublisher(std::shared_ptr<MqttRoot> mqttRoot, TelemetryCollector& telemetryCollector)
        : mqttRoot(mqttRoot)
        , telemetryCollector(telemetryCollector) {
    }

    void publishTelemetry() {
        mqttRoot->publish("telemetry", [&](JsonObject& json) { telemetryCollector.collect(json); }, Retention::NoRetain, QoS::AtLeastOnce);
    }

private:
    std::shared_ptr<MqttRoot> mqttRoot;
    TelemetryCollector& telemetryCollector;
};

class Device {
public:
    Device(
        const std::shared_ptr<TDeviceConfiguration> deviceConfig,
        const std::shared_ptr<TDeviceDefinition> deviceDefinition,
        std::shared_ptr<BatteryManager> battery,
        std::shared_ptr<PowerManager> powerManager,
        std::shared_ptr<Kernel> kernel,
        std::shared_ptr<MqttRoot> mqttDeviceRoot)
        : deviceDefinition(deviceDefinition)
        , kernel(kernel)
        , mqttDeviceRoot(mqttDeviceRoot)
#ifdef FARMHUB_DEBUG
        , debugConsole(battery, kernel->wifi)
#endif
    {
        kernel->switches->onReleased("factory-reset", deviceDefinition->bootPin, SwitchMode::PullUp, [this](const Switch&, milliseconds duration) {
            if (duration >= 15s) {
                LOGI("Factory reset triggered after %lld ms", duration.count());
                this->kernel->performFactoryReset(true);
            } else if (duration >= 5s) {
                LOGI("WiFi reset triggered after %lld ms", duration.count());
                this->kernel->performFactoryReset(false);
            }
        });

        if (battery != nullptr) {
            deviceTelemetryCollector.registerProvider("battery", battery);
            kernel->shutdownManager->registerShutdownListener([this]() {
                peripheralManager.shutdown();
            });
            LOGI("Battery configured");
        } else {
            LOGI("No battery configured");
        }

        deviceTelemetryCollector.registerProvider("wifi", std::make_shared<WiFiTelemetryProvider>(kernel->wifi));

#if defined(FARMHUB_DEBUG) || defined(FARMHUB_REPORT_MEMORY)
        deviceTelemetryCollector.registerProvider("memory", std::make_shared<MemoryTelemetryProvider>());
#endif
        deviceTelemetryCollector.registerProvider("pm", std::make_shared<PowerManagementTelemetryProvider>(powerManager));

        deviceDefinition->registerPeripheralFactories(peripheralManager);

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
        kernel->getRtcInSyncState().awaitSet();

        JsonDocument peripheralsInitDoc;
        JsonArray peripheralsInitJson = peripheralsInitDoc.to<JsonArray>();

        auto builtInPeripheralsConfig = deviceDefinition->getBuiltInPeripherals();
        LOGD("Loading configuration for %d built-in peripherals",
            builtInPeripheralsConfig.size());
        for (auto& peripheralConfig : builtInPeripheralsConfig) {
            peripheralManager.createPeripheral(peripheralConfig, peripheralsInitJson);
        }

        auto& peripheralsConfig = deviceConfig->peripherals.get();
        LOGI("Loading configuration for %d user-configured peripherals",
            peripheralsConfig.size());
        bool peripheralError = false;
        for (auto& peripheralConfig : peripheralsConfig) {
            if (!peripheralManager.createPeripheral(peripheralConfig.get(), peripheralsInitJson)) {
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
                json["version"] = kernel->version;
                json["reset"] = esp_reset_reason();
                json["wakeup"] = esp_sleep_get_wakeup_cause();
                json["bootCount"] = bootCount++;
                json["time"] = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
                json["state"] = static_cast<int>(initState);
                json["peripherals"].to<JsonArray>().set(peripheralsInitJson);
                json["sleepWhenIdle"] = powerManager->sleepWhenIdle;

                reportPreviousCrashIfAny(json);
            },
            Retention::NoRetain, QoS::AtLeastOnce, 5s);

        auto publishInterval = deviceConfig->publishInterval.get();
        Task::loop("telemetry", 8192, [this, publishInterval](Task& task) {
            task.markWakeTime();

            publishTelemetry();

            // Signal that we are still alive
            this->kernel->watchdog.restart();

            // We always wait at least this much between telemetry updates
            const auto debounceInterval = 500ms;
            // Delay without updating last wake time
            task.delay(task.ticksUntil(debounceInterval));

            // Allow other tasks to trigger telemetry updates
            auto timeout = task.ticksUntil(publishInterval - debounceInterval);
            telemetryPublishQueue.pollIn(timeout);
        });

        kernel->getKernelReadyState().set();

        LOGI("Device ready in %.2f s (kernel version %s on %s instance '%s' with hostname '%s' and IP '%s', SSID '%s', current time is %lld)",
            duration_cast<milliseconds>(boot_clock::now().time_since_epoch()).count() / 1000.0,
            kernel->version.c_str(),
            deviceConfig->model.get().c_str(),
            deviceConfig->instance.get().c_str(),
            deviceConfig->getHostname().c_str(),
            kernel->wifi->getIp().value_or("<no-ip>").c_str(),
            kernel->wifi->getSsid().value_or("<no-ssid>").c_str(),
            duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
    }

private:
    enum class InitState {
        Success = 0,
        PeripheralError = 1,
    };

    void publishTelemetry() {
        deviceTelemetryPublisher.publishTelemetry();
        peripheralManager.publishTelemetry();
    }

    void reportPreviousCrashIfAny(JsonObject& json) {
        if (!hasCoreDump()) {
            return;
        }

        esp_core_dump_summary_t summary {};
        esp_err_t err = esp_core_dump_get_summary(&summary);
        if (err != ESP_OK) {
            LOGE("Failed to get core dump summary: %s", esp_err_to_name(err));
        } else {
            auto crashJson = json["crash"].to<JsonObject>();
            reportPreviousCrash(crashJson, summary);
        }

        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_core_dump_image_erase());
    }

    bool hasCoreDump() {
        esp_err_t err = esp_core_dump_image_check();
        switch (err) {
            case ESP_OK:
                return true;
            case ESP_ERR_NOT_FOUND:
                LOGV("No core dump found");
                return false;
            case ESP_ERR_INVALID_SIZE:
                LOGD("Invalid core dump size");
                // Typically, this happens when no core dump has been saved yet
                return false;
            default:
                LOGE("Failed to check for core dump: %s", esp_err_to_name(err));
                return false;
        }
    }

    void reportPreviousCrash(JsonObject& json, const esp_core_dump_summary_t& summary) {
        auto excCause =
#if __XTENSA__
            summary.ex_info.exc_cause;
#else
            summary.ex_info.mcause;
#endif

        LOGW("Core dump found: task: %s, cause: %ld",
            summary.exc_task, excCause);

        json["version"] = summary.core_dump_version;
        json["sha256"] = std::string((const char*) summary.app_elf_sha256);
        json["task"] = std::string(summary.exc_task);
        json["cause"] = excCause;

        static constexpr size_t PANIC_REASON_SIZE = 256;
        char panicReason[PANIC_REASON_SIZE];
        if (esp_core_dump_get_panic_reason(panicReason, PANIC_REASON_SIZE) == ESP_OK) {
            LOGW("Panic reason: %s", panicReason);
            json["panicReason"] = std::string(panicReason);
        }

#ifdef __XTENSA__
        auto backtraceJson = json["backtrace"].to<JsonObject>();
        if (summary.exc_bt_info.corrupted) {
            LOGE("Backtrace corrupted, depth %lu", summary.exc_bt_info.depth);
            backtraceJson["corrupted"] = true;
        } else {
            auto framesJson = backtraceJson["frames"].to<JsonArray>();
            for (int i = 0; i < summary.exc_bt_info.depth; i++) {
                auto& frame = summary.exc_bt_info.bt[i];
                framesJson.add("0x" + toHexString(frame));
            }
        }
#else
        size_t encodedLen = (summary.exc_bt_info.dump_size + 2) / 3 * 4 + 1;
        unsigned char encoded[encodedLen];

        size_t writtenLen = 0;
        int ret = mbedtls_base64_encode(encoded, sizeof(encoded), &writtenLen, summary.exc_bt_info.stackdump, summary.exc_bt_info.dump_size);

        if (ret == 0) {
            encoded[writtenLen] = '\0';    // Null-terminate the output string
            json["stackdump"] = encoded;
        } else {
            LOGE("Failed to encode stackdump: %d", ret);
        }
#endif
    }

    const std::shared_ptr<TDeviceDefinition> deviceDefinition;
    const std::shared_ptr<Kernel> kernel;
    const std::shared_ptr<MqttRoot> mqttDeviceRoot;

#ifdef FARMHUB_DEBUG
    DebugConsole debugConsole;
#endif

    PeripheralManager peripheralManager { kernel->i2c, deviceDefinition->pcnt, deviceDefinition->pulseCounterManager, deviceDefinition->pwm, kernel->switches, mqttDeviceRoot };

    TelemetryCollector deviceTelemetryCollector;
    MqttTelemetryPublisher deviceTelemetryPublisher { mqttDeviceRoot, deviceTelemetryCollector };
    PingCommand pingCommand { [this]() {
        telemetryPublishQueue.offer(true);
    } };

    FileSystem& fs { kernel->fs };
    EchoCommand echoCommand;
    RestartCommand restartCommand;
    SleepCommand sleepCommand;
    FileListCommand fileListCommand { fs };
    FileReadCommand fileReadCommand { fs };
    FileWriteCommand fileWriteCommand { fs };
    FileRemoveCommand fileRemoveCommand { fs };
    HttpUpdateCommand httpUpdateCommand { [this](const std::string& url) {
        kernel->prepareUpdate(url);
    } };

    CopyQueue<bool> telemetryPublishQueue { "telemetry-publish", 1 };
};

}    // namespace farmhub::devices
