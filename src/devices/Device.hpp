#pragma once

#include <chrono>

#ifndef FARMHUB_LOG_LEVEL
#ifdef FARMHUB_DEBUG
#define FARMHUB_LOG_LEVEL LOG_LEVEL_VERBOSE
#else
#define FARMHUB_LOG_LEVEL LOG_LEVEL_INFO
#endif
#endif

#include <Print.h>

#include <ArduinoLog.h>

#include <kernel/Kernel.hpp>
#include <peripherals/Valve.hpp>

using namespace std::chrono;
using namespace farmhub::kernel;
using namespace farmhub::peripherals;

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

namespace farmhub { namespace devices {

class ConsolePrinter : public Print {
public:
    ConsolePrinter() {
        Serial.begin(115200);

        static const String spinner = "|/-\\";
        static const int spinnerLength = spinner.length();
        Task::loop("ConsolePrinter", 8192, 1, [this](Task& task) {
            String status;
            status += "\033[1G\033[0K";

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
            status += " ";

            Serial.print(status);
            task.delayUntil(milliseconds(100));
        });
    }

    void registerBattery(BatteryDriver& battery) {
        this->battery = &battery;
    }

    size_t write(uint8_t character) override {
        getBuffer()->concat((char) character);
        return 1;
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        getBuffer()->concat(buffer, size);
        return size;
    }

    void printLine(int level) {
        String* buffer = getBuffer();
        if (buffer->length() > 0) {
            Serial.print(buffer->c_str());
            buffer->clear();
        }
    }

private:
    String* getBuffer() {
        String* buffer = static_cast<String*>(pvTaskGetThreadLocalStoragePointer(nullptr, Task::CONSOLE_BUFFER_INDEX));
        if (buffer == nullptr) {
            Serial.printf("\033[0;31mTask %s has no console buffer\033[0m\n", pcTaskGetName(nullptr));
            buffer = new String();
            vTaskSetThreadLocalStoragePointer(nullptr, Task::CONSOLE_BUFFER_INDEX, static_cast<void*>(buffer));
        }
        return buffer;
    }

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
};

#ifdef FARMHUB_DEBUG
ConsolePrinter consolePrinter;

void printLogLine(Print* printer, int level) {
    consolePrinter.printLine(level);
}
#endif

class ConsoleProvider {
public:
    ConsoleProvider() {
#ifdef FARMHUB_DEBUG
        Log.begin(FARMHUB_LOG_LEVEL, &consolePrinter);
        Log.setSuffix(printLogLine);
#else
        Log.begin(FARMHUB_LOG_LEVEL, &Serial);
#endif
        Log.infoln("Starting up...");
    }
};

class Device : ConsoleProvider {
public:
    Device() {

#ifdef HAS_BATTERY
#ifdef FARMHUB_DEBUG
        consolePrinter.registerBattery(deviceDefinition.batteryDriver);
#endif
        kernel.registerTelemetryProvider("battery", deviceDefinition.batteryDriver);
#endif

        deviceDefinition.registerPeripheralFactories(peripheralManager);

        kernel.begin();

        peripheralManager.begin();

#if defined(MK4)
        deviceDefinition.motorDriver.wakeUp();
#elif defined(MK5)
        deviceDefinition.motorADriver.wakeUp();
#elif defined(MK6)
        deviceDefinition.motorDriver.wakeUp();
#endif
    }

private:
    TDeviceDefinition deviceDefinition;
    Kernel<TDeviceConfiguration> kernel { deviceDefinition.statusLed };
    PeripheralManager peripheralManager;
};

}}    // namespace farmhub::devices
