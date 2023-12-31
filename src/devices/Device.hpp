#pragma once

#include <chrono>
#include <memory>

#ifndef FARMHUB_LOG_LEVEL
#ifdef FARMHUB_DEBUG
#define FARMHUB_LOG_LEVEL LOG_LEVEL_VERBOSE
#else
#define FARMHUB_LOG_LEVEL LOG_LEVEL_INFO
#endif
#endif

#include <Print.h>

#include <ArduinoLog.h>

#include <kernel/Command.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/Task.hpp>

using namespace std::chrono;
using std::shared_ptr;
using namespace farmhub::kernel;

#if defined(MK4)
#include <devices/UglyDucklingMk4.hpp>
typedef farmhub::devices::UglyDucklingMk4 TDeviceDefinition;
typedef farmhub::devices::Mk4Config TDeviceConfiguration;

#elif defined(MK5)
#include <devices/UglyDucklingMk5.hpp>
typedef farmhub::devices::UglyDucklingMk5 TDeviceDefinition;
typedef farmhub::devices::Mk5Config TDeviceConfiguration;
#define HAS_BATTERY

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
class ConsolePrinter : public Print {
public:
    ConsolePrinter() {
        static const String spinner = "|/-\\";
        static const int spinnerLength = spinner.length();
        Task::loop("console", 8192, 1, [this](Task& task) {
            String status;

            counter = (counter + 1) % spinnerLength;
            status += "[" + spinner.substring(counter, counter + 1) + "] ";

            status += "\033[33m" + String(VERSION) + "\033[0m";

            status += ", IP: \033[33m" + WiFi.localIP().toString() + "\033[0m";
            status += "/" + wifiStatus();

            status += ", uptime: \033[33m" + String(float(millis()) / 1000.0f, 1) + "\033[0m s";
            time_t now;
            struct tm timeinfo;
            time(&now);
            localtime_r(&now, &timeinfo);

            char buffer[64];    // Ensure buffer is large enough for the formatted string
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
            status += ", UTC: \033[33m" + String(buffer) + "\033[0m";

            status += ", heap: \033[33m" + String(float(ESP.getFreeHeap()) / 1024.0f, 2) + "\033[0m kB";

            BatteryDriver* battery = this->battery.load();
            if (battery != nullptr) {
                status += ", battery: \033[33m" + String(battery->getVoltage(), 2) + "\033[0m V";
            }

            Serial.print("\033[1G\033[0K");

            consoleQueue.drain([](String line) {
                Serial.println(line);
            });

            Serial.print(status);
            task.delayUntil(milliseconds(100));
        });
    }

    void registerBattery(BatteryDriver& battery) {
        this->battery = &battery;
    }

    size_t write(uint8_t character) override {
        Task::consoleBuffer()->concat((char) character);
        return 1;
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        Task::consoleBuffer()->concat(buffer, size);
        return size;
    }

    void printLine(int level) {
        String* buffer = Task::consoleBuffer();
        if (buffer->isEmpty()) {
            return;
        }
        size_t pos = (buffer->startsWith("\r") || buffer->startsWith("\n")) ? 1 : 0;
        size_t len = buffer->endsWith("\n") ? buffer->length() - 1 : buffer->length();
        String copy = "[\033[0;31m" + String(pcTaskGetName(nullptr)) + "\033[0m/\033[0;32m" + String(xPortGetCoreID()) + "\033[0m] " + buffer->substring(pos, pos + len);
        buffer->clear();
        if (!consoleQueue.offerFromISR(copy)) {
            Serial.println(copy);
        }
    }

private:
    static String wifiStatus() {
        switch (WiFi.status()) {
            case WL_NO_SHIELD:
                return "\033[0;31mno shield\033[0m";
            case WL_IDLE_STATUS:
                return "\033[0;33midle\033[0m";
            case WL_NO_SSID_AVAIL:
                return "\033[0;31mno SSID\033[0m";
            case WL_SCAN_COMPLETED:
                return "\033[0;33mscan completed\033[0m";
            case WL_CONNECTED:
                return "\033[0;32mOK\033[0m";
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
    std::atomic<BatteryDriver*> battery { nullptr };

    Queue<String> consoleQueue { "console", 128 };
};

ConsolePrinter consolePrinter;

void printLogLine(Print* printer, int level) {
    consolePrinter.printLine(level);
}
#endif

class ConsoleProvider {
public:
    ConsoleProvider() {
        Serial.begin(115200);
#ifdef FARMHUB_DEBUG
        Log.begin(FARMHUB_LOG_LEVEL, &consolePrinter);
        Log.setSuffix(printLogLine);
#else
        Log.begin(FARMHUB_LOG_LEVEL, &Serial);
#endif
        Log.infoln(F("  ______                   _    _       _"));
        Log.infoln(F(" |  ____|                 | |  | |     | |"));
        Log.infoln(F(" | |__ __ _ _ __ _ __ ___ | |__| |_   _| |__"));
        Log.infoln(F(" |  __/ _` | '__| '_ ` _ \\|  __  | | | | '_ \\"));
        Log.infoln(F(" | | | (_| | |  | | | | | | |  | | |_| | |_) |"));
        Log.infoln(F(" |_|  \\__,_|_|  |_| |_| |_|_|  |_|\\__,_|_.__/ %s"), VERSION);
        Log.infoln("");
    }
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

class Device : ConsoleProvider {
public:
    Device() {

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

        // deviceTelemetryCollector.registerProvider("peripherals", peripheralManager);

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

#if defined(MK4)
        deviceDefinition.motorDriver.wakeUp();
#elif defined(MK5)
        deviceDefinition.motorADriver.wakeUp();
#elif defined(MK6)
        deviceDefinition.motorDriver.wakeUp();
#endif

        // We want RTC to be in sync before we start setting up peripherals
        kernel.getRtcInSyncState().awaitSet();

        peripheralManager.createPeripherals("ugly-duckling/" + deviceConfig.instance.get());

        kernel.getKernelReadyState().awaitSet();

        mqttDeviceRoot->publish(
            "init",
            [&](JsonObject& json) {
                // TODO Remove redundant mentions of "ugly-duckling"
                json["type"] = "ugly-duckling";
                json["model"] = deviceConfig.model.get();
                json["instance"] = deviceConfig.instance.get();
                json["mac"] = getMacAddress();
                auto device = json.createNestedObject("deviceConfig");
                deviceConfig.store(device, false);
                // TODO Remove redundant mentions of "ugly-duckling"
                json["app"] = "ugly-duckling";
                json["version"] = kernel.version;
                json["wakeup"] = esp_sleep_get_wakeup_cause();
                json["bootCount"] = bootCount++;
                json["time"] = time(nullptr);
            });
        Task::loop("telemetry", 8192, [this](Task& task) { publishTelemetry(task); });
    }

private:
    void publishTelemetry(Task& task) {
        deviceTelemetryPublisher.publishTelemetry();
        peripheralManager.publishTelemetry();
        // TODO Configure telemetry heartbeat interval
        task.delayUntil(milliseconds(60000));
    }

    TDeviceDefinition deviceDefinition;
    TDeviceConfiguration& deviceConfig = deviceDefinition.config;
    Kernel<TDeviceConfiguration> kernel { deviceConfig, deviceDefinition.statusLed };
    PeripheralManager peripheralManager { kernel.mqtt, deviceConfig.peripherals };

    TelemetryCollector deviceTelemetryCollector;
    shared_ptr<MqttDriver::MqttRoot> mqttDeviceRoot = kernel.mqtt.forRoot("devices/ugly-duckling/" + deviceConfig.instance.get());
    MqttTelemetryPublisher deviceTelemetryPublisher { mqttDeviceRoot, deviceTelemetryCollector };
    PingCommand pingCommand { deviceTelemetryPublisher };

#if defined(FARMHUB_DEBUG) || defined(FARMHUB_REPORT_MEMORY)
    MemoryTelemetryProvider memoryTelemetryProvider;
#endif

    FileSystem& fs { FileSystem::get() };
    EchoCommand echoCommand;
    RestartCommand restartCommand;
    SleepCommand sleepCommand;
    FileListCommand fileListCommand { fs };
    FileReadCommand fileReadCommand { fs };
    FileWriteCommand fileWriteCommand { fs };
    FileRemoveCommand fileRemoveCommand { fs };
    HttpUpdateCommand httpUpdateCommand { kernel.version };
};

}    // namespace farmhub::devices
