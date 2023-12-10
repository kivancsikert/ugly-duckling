#pragma once

#include <kernel/Application.hpp>
#include <kernel/PwmManager.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/LedDriver.hpp>
#include <version.h>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

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

class MemoryTelemetryProvider : public TelemetryProvider {
public:
    void populateTelemetry(JsonObject& json) override {
        json["free-heap"] = ESP.getFreeHeap();
    }
};

class ConsoleProvider {
public:
    ConsoleProvider() {
        Serial.begin(115200);
        Serial.println("Starting up...");
    }
};

template <typename TDeviceConfiguration>
class Device : ConsoleProvider {
public:
    Device(gpio_num_t statusPin)
        : statusLed("status", statusPin) {
#if defined(FARMHUB_DEBUG) || defined(FARMHUB_REPORT_MEMORY)
        application.registerTelemetryProvider("memory", memoryTelemetryProvider);
#endif
    }

protected:
#ifdef FARMHUB_DEBUG
    ConsolePrinter consolePrinter;
#endif

#if defined(FARMHUB_DEBUG) || defined(FARMHUB_REPORT_MEMORY)
    MemoryTelemetryProvider memoryTelemetryProvider;
#endif

public:
    LedDriver statusLed;
    Application<TDeviceConfiguration> application { statusLed };
    PwmManager pwm;
};

template <typename TDeviceConfiguration>
class BatteryPoweredDevice : public Device<TDeviceConfiguration> {
public:
    BatteryPoweredDevice(gpio_num_t statusPin, gpio_num_t batteryPin, float batteryVoltageDividerRatio)
        : Device<TDeviceConfiguration>(statusPin)
        , batteryDriver(batteryPin, batteryVoltageDividerRatio) {
#ifdef FARMHUB_DEBUG
        Device<TDeviceConfiguration>::consolePrinter.registerBattery(batteryDriver);
#endif
        Device<TDeviceConfiguration>::application.registerTelemetryProvider("battery", batteryDriver);
    }

public:
    BatteryDriver batteryDriver;
};

}}    // namespace farmhub::devices
