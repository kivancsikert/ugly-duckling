#pragma once

#include <chrono>
#include <memory>

#include <Arduino.h>

#include <esp32/clk.h>
#include <esp_pm.h>

#include <Print.h>

#include <kernel/Command.hpp>
#include <kernel/Concurrent.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/Log.hpp>
#include <kernel/Task.hpp>
#include <kernel/drivers/RtcDriver.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;
using std::shared_ptr;
using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

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
#define HAS_BATTERY

#else
#error "No device defined"
#endif

namespace farmhub::devices {

#ifdef FARMHUB_DEBUG
class ConsolePrinter {
public:
    ConsolePrinter() {
        static const String spinner = "|/-\\";
        static const int spinnerLength = spinner.length();
        status.reserve(256);
        Task::loop("console", 3072, 1, [this](Task& task) {
            status.clear();

            counter = (counter + 1) % spinnerLength;
            status.concat("[");
            status.concat(spinner.substring(counter, counter + 1));
            status.concat("] ");

            status.concat("\033[33m");
            status.concat(VERSION);
            status.concat("\033[0m");

            status.concat(", WIFI: ");
            status.concat(wifiStatus());

            status.concat(", uptime: \033[33m");
            status.concat(String(float(millis()) / 1000.0f, 1));
            status.concat("\033[0m s");

            status.concat(", RTC: \033[33m");
            status.concat(RtcDriver::isTimeSet() ? "OK" : "UNSYNCED");
            status.concat("\033[0m");

            status.concat(", heap: \033[33m");
            status.concat(String(float(ESP.getFreeHeap()) / 1024.0f, 2));
            status.concat("\033[0m kB");

            status.concat(", CPU: \033[33m");
            status.concat(esp_clk_cpu_freq() / 1000000);
            status.concat("\033[0m MHz");

            BatteryDriver* battery = this->battery.load();
            if (battery != nullptr) {
                status.concat(", battery: \033[33m");
                status.concat(String(battery->getVoltage(), 2));
                status.concat("\033[0m V");
            }

            Serial.print("\033[1G\033[0K");

            consoleQueue.drain([](const String& line) {
                Serial.println(line);
#if Serial != Serial0
                Serial0.println(line);
#endif
            });

            Serial.print(status);
            task.delayUntil(100ms);
        });
    }

    void registerBattery(BatteryDriver& battery) {
        this->battery = &battery;
    }

    void printLog(Level level, const char* message) {
        sprintf(timeBuffer, "%8.3f", millis() / 1000.0);
        String* buffer = new String();
        buffer->reserve(256);
        buffer->concat("\033[0;90m");
        buffer->concat(timeBuffer);
        buffer->concat("\033[0m [\033[0;31m");
        buffer->concat(pcTaskGetName(nullptr));
        buffer->concat("\033[0m/\033[0;32m");
        buffer->concat(xPortGetCoreID());
        buffer->concat("\033[0m] ");
        switch (level) {
            case Level::Fatal:
                buffer->concat("\033[0;31mFATAL\033[0m ");
                break;
            case Level::Error:
                buffer->concat("\033[0;31mERROR\033[0m ");
                break;
            case Level::Warning:
                buffer->concat("\033[0;33mWARNING\033[0m ");
                break;
            case Level::Info:
                buffer->concat("\033[0;32mINFO\033[0m ");
                break;
            case Level::Debug:
                buffer->concat("\033[0;34mDEBUG\033[0m ");
                break;
            case Level::Trace:
                buffer->concat("\033[0;36mTRACE\033[0m ");
                break;
            default:
                break;
        }
        buffer->concat(message);
        if (!consoleQueue.offer(*buffer)) {
            Serial.println(*buffer);
#if Serial != Serial0
            Serial0.println(*buffer);
#endif
        }
        delete buffer;
    }

private:
    static String wifiStatus() {
        switch (WiFi.status()) {
            case WL_NO_SHIELD:
                return "\033[0;31mno shield\033[0m";
            case WL_IDLE_STATUS:
                return "\033[0;31midle\033[0m";
            case WL_NO_SSID_AVAIL:
                return "\033[0;31mno SSID\033[0m";
            case WL_SCAN_COMPLETED:
                return "\033[0;33mscan completed\033[0m";
            case WL_CONNECTED:
                return "\033[0;33m" + WiFi.localIP().toString() + "\033[0m";
            case WL_CONNECT_FAILED:
                return "\033[0;31mfailed\033[0m";
            case WL_CONNECTION_LOST:
                return "\033[0;31mconnection lost\033[0m";
            case WL_DISCONNECTED:
                return "\033[0;33mdisconnected\033[0m";
            default:
                return "\033[0;31munknown\033[0m";
        }
    }

    int counter;
    char timeBuffer[12];
    String status;
    std::atomic<BatteryDriver*> battery { nullptr };

    Queue<String> consoleQueue { "console", 128 };
};

ConsolePrinter consolePrinter;
#endif

struct LogRecord {
    Level level;
    String message;
};

class ConsoleProvider : public LogConsumer {
public:
    ConsoleProvider(Queue<LogRecord>& logRecords, Level recordedLevel)
        : logRecords(logRecords)
        , recordedLevel(recordedLevel) {
        Serial.begin(115200);
#if Serial != Serial0
        Serial0.begin(115200);
#endif
        Log.setConsumer(this);
        Log.log(Level::Info, F("  ______                   _    _       _"));
        Log.log(Level::Info, F(" |  ____|                 | |  | |     | |"));
        Log.log(Level::Info, F(" | |__ __ _ _ __ _ __ ___ | |__| |_   _| |__"));
        Log.log(Level::Info, F(" |  __/ _` | '__| '_ ` _ \\|  __  | | | | '_ \\"));
        Log.log(Level::Info, F(" | | | (_| | |  | | | | | | |  | | |_| | |_) |"));
        Log.log(Level::Info, F(" |_|  \\__,_|_|  |_| |_| |_|_|  |_|\\__,_|_.__/ %s"), VERSION);
    }

    void consumeLog(Level level, const char* message) override {
        if (level <= recordedLevel) {
            logRecords.offer(LogRecord { level, message });
        }
#ifdef FARMHUB_DEBUG
        consolePrinter.printLog(level, message);
#else
        Serial.println(message);
#if Serial != Serial0
        Serial0.println(message);
#endif
#endif
    }

private:
    Queue<LogRecord>& logRecords;
    const Level recordedLevel;
};

class MemoryTelemetryProvider : public TelemetryProvider {
public:
    void populateTelemetry(JsonObject& json) override {
        json["free-heap"] = ESP.getFreeHeap();
    }
};

class MqttTelemetryPublisher : public TelemetryPublisher {
public:
    MqttTelemetryPublisher(shared_ptr<MqttDriver::MqttRoot> mqttRoot, TelemetryCollector& telemetryCollector)
        : mqttRoot(mqttRoot)
        , telemetryCollector(telemetryCollector) {
    }

    void publishTelemetry() {
        mqttRoot->publish("telemetry", [&](JsonObject& json) { telemetryCollector.collect(json); });
    }

private:
    shared_ptr<MqttDriver::MqttRoot> mqttRoot;
    TelemetryCollector& telemetryCollector;
};

class ConfiguredKernel {
public:
    ConfiguredKernel(Queue<LogRecord>& logRecords)
        : consoleProvider(logRecords, deviceDefinition.config.publishLogs.get()) {
    }

    TDeviceDefinition deviceDefinition;
    ConsoleProvider consoleProvider;
    Kernel<TDeviceConfiguration> kernel { deviceDefinition.config, deviceDefinition.mqttConfig, deviceDefinition.statusLed };
};

class Device {
public:
    Device() {
        kernel.switches.onReleased("factory-reset", deviceDefinition.bootPin, SwitchMode::PullUp, [this](const Switch&, milliseconds duration) {
            if (duration >= 5s) {
                kernel.performFactoryReset();
            }
        });

#ifdef HAS_BATTERY

#ifdef FARMHUB_DEBUG

        consolePrinter.registerBattery(deviceDefinition.batteryDriver);
#endif
        deviceTelemetryCollector.registerProvider("battery", deviceDefinition.batteryDriver);
#endif

#if defined(FARMHUB_DEBUG) || defined(FARMHUB_REPORT_MEMORY)
        deviceTelemetryCollector.registerProvider("memory", memoryTelemetryProvider);
#endif

        deviceDefinition.registerPeripheralFactories(peripheralManager);

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

        Task::loop("mqtt:log", 3072, [this](Task& task) {
            logRecords.take([&](const LogRecord& record) {
                if (record.level > deviceConfig.publishLogs.get()) {
                    return;
                }

                mqttDeviceRoot->publish(
                    "log", [&](JsonObject& json) {
                        json["level"] = record.level;
                        json["message"] = record.message;
                    },
                    MqttDriver::Retention::NoRetain, MqttDriver::QoS::AtLeastOnce, ticks::zero(), MqttDriver::LogPublish::Silent);
            });
        });

        // We want RTC to be in sync before we start setting up peripherals
        kernel.getRtcInSyncState().awaitSet();

        auto builtInPeripheralsCofig = deviceDefinition.getBuiltInPeripherals();
        Log.debug("Loading configuration for %d built-in peripherals",
            builtInPeripheralsCofig.size());
        for (auto& perpheralConfig : builtInPeripheralsCofig) {
            peripheralManager.createPeripheral(perpheralConfig);
        }

        auto& peripheralsConfig = deviceConfig.peripherals.get();
        Log.info("Loading configuration for %d user-configured peripherals",
            peripheralsConfig.size());
        for (auto& perpheralConfig : peripheralsConfig) {
            peripheralManager.createPeripheral(perpheralConfig.get());
        }

        mqttDeviceRoot->publish(
            "init",
            [&](JsonObject& json) {
                // TODO Remove redundant mentions of "ugly-duckling"
                json["type"] = "ugly-duckling";
                json["model"] = deviceConfig.model.get();
                json["id"] = deviceConfig.id.get();
                json["instance"] = deviceConfig.instance.get();
                json["mac"] = getMacAddress();
                auto device = json["deviceConfig"].to<JsonObject>();
                deviceConfig.store(device, false);
                // TODO Remove redundant mentions of "ugly-duckling"
                json["app"] = "ugly-duckling";
                json["version"] = kernel.version;
                json["wakeup"] = esp_sleep_get_wakeup_cause();
                json["bootCount"] = bootCount++;
                json["time"] = time(nullptr);
                json["sleepWhenIdle"] = kernel.sleepManager.sleepWhenIdle;
            },
            MqttDriver::Retention::NoRetain, MqttDriver::QoS::AtLeastOnce, ticks::max());

        Task::loop("telemetry", 8192, [this](Task& task) {
            publishTelemetry();
            // TODO Configure these telemetry intervals
            // Publishing interval
            const auto interval = 1min;
            // We always wait at least this much between telemetry updates
            const auto debounceInterval = 1s;
            task.delayUntil(debounceInterval);
            // Allow other tasks to trigger telemetry updates
            telemetryPublishQueue.pollIn(task.ticksUntil(interval - debounceInterval));
        });

        kernel.getKernelReadyState().set();

        Log.info("Device ready in %.2f s (kernel version %s on %s instance '%s' with hostname '%s' and IP '%s')",
            millis() / 1000.0,
            kernel.version.c_str(),
            deviceConfig.model.get().c_str(),
            deviceConfig.instance.get().c_str(),
            deviceConfig.getHostname().c_str(),
            WiFi.localIP().toString().c_str());
    }

private:
    void publishTelemetry() {
        deviceTelemetryPublisher.publishTelemetry();
        peripheralManager.publishTelemetry();
    }

    String locationPrefix() {
        if (deviceConfig.location.hasValue()) {
            return deviceConfig.location.get() + "/";
        } else {
            return "";
        }
    }

    Queue<LogRecord> logRecords { "logs", 32 };
    ConfiguredKernel configuredKernel { logRecords };
    Kernel<TDeviceConfiguration>& kernel = configuredKernel.kernel;
    TDeviceDefinition& deviceDefinition = configuredKernel.deviceDefinition;
    TDeviceConfiguration& deviceConfig = deviceDefinition.config;

    shared_ptr<MqttDriver::MqttRoot> mqttDeviceRoot = kernel.mqtt.forRoot(locationPrefix() + "devices/ugly-duckling/" + deviceConfig.instance.get());
    PeripheralManager peripheralManager { kernel.i2c, deviceDefinition.pcnt, deviceDefinition.pwm, kernel.sleepManager, kernel.switches, mqttDeviceRoot };

    TelemetryCollector deviceTelemetryCollector;
    MqttTelemetryPublisher deviceTelemetryPublisher { mqttDeviceRoot, deviceTelemetryCollector };
    PingCommand pingCommand { [this]() {
        telemetryPublishQueue.offer(true);
    } };

#if defined(FARMHUB_DEBUG) || defined(FARMHUB_REPORT_MEMORY)
    MemoryTelemetryProvider memoryTelemetryProvider;
#endif

    FileSystem& fs { kernel.fs };
    EchoCommand echoCommand;
    RestartCommand restartCommand;
    SleepCommand sleepCommand;
    FileListCommand fileListCommand { fs };
    FileReadCommand fileReadCommand { fs };
    FileWriteCommand fileWriteCommand { fs };
    FileRemoveCommand fileRemoveCommand { fs };
    HttpUpdateCommand httpUpdateCommand { [this](const String& url) {
        kernel.prepareUpdate(url);
    } };

    Queue<bool> telemetryPublishQueue { "telemetry-publish", 1 };
};

}    // namespace farmhub::devices
