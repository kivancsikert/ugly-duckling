#pragma once

#include <chrono>

#ifndef FARMHUB_LOG_LEVEL
#ifdef FARMHUB_DEBUG
#define FARMHUB_LOG_LEVEL LOG_LEVEL_VERBOSE
#else
#define FARMHUB_LOG_LEVEL LOG_LEVEL_INFO
#endif
#endif

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

class ConsolePrinter {
public:
    ConsolePrinter() {
        static const String spinner = "|/-\\";
        Task::loop("ConsolePrinter", 8192, 1, [this](Task& task) {
            Serial.print("\033[1G\033[0K");

            counter = (counter + 1) % spinner.length();
            Serial.print("[" + spinner.substring(counter, counter + 1) + "] ");

            Serial.print("\033[33m" + String(VERSION) + "\033[0m");

            Serial.print(", IP: \033[33m" + WiFi.localIP().toString() + "\033[0m");
            Serial.print("/" + wifiStatus());

            Serial.printf(", uptime: \033[33m%.1f\033[0m s", float(millis()) / 1000.0f);
            time_t now;
            struct tm timeinfo;
            time(&now);
            localtime_r(&now, &timeinfo);
            Serial.print(&timeinfo, ", UTC: \033[33m%Y-%m-%d %H:%M:%S\033[0m");

            Serial.printf(", heap: \033[33m%.2f\033[0m kB", float(ESP.getFreeHeap()) / 1024.0f);

            BatteryDriver* battery = this->battery.load();
            if (battery != nullptr) {
                Serial.printf(", battery: \033[33m%.2f V\033[0m", battery->getVoltage());
            }

            Serial.print(" ");
            Serial.flush();
            task.delayUntil(milliseconds(100));
        });
    }

    void registerBattery(BatteryDriver& battery) {
        this->battery = &battery;
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
};

class ConsoleProvider {
public:
    ConsoleProvider() {
        Serial.begin(115200);

        Log.begin(FARMHUB_LOG_LEVEL, &Serial);
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

#ifdef FARMHUB_DEBUG
    ConsolePrinter consolePrinter;
#endif

    TDeviceDefinition deviceDefinition;
    Kernel<TDeviceConfiguration> kernel { deviceDefinition.statusLed };
    PeripheralManager peripheralManager;
};

}}    // namespace farmhub::devices
