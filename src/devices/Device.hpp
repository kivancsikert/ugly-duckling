#pragma once

#include <kernel/Application.hpp>
#include <kernel/PwmManager.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/LedDriver.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

namespace farmhub { namespace devices {

class ConsolePrinter {
public:
    ConsolePrinter() {
        static const String spinner = "|/-\\";
        Task::loop("ConsolePrinter", 32 * 1024, 1, [this](Task& task) {
            Serial.print("\033[1G\033[0K");

            counter = (counter + 1) % spinner.length();
            Serial.print("[" + spinner.substring(counter, counter + 1) + "] ");

            Serial.print("\033[33m" + wifiStatus() + "\033[0m");
            Serial.print(", IP: \033[33m" + WiFi.localIP().toString() + "\033[0m");

            Serial.print(", uptime: \033[33m" + String(millis()) + "\033[0m ms");
            time_t now;
            struct tm timeinfo;
            time(&now);
            localtime_r(&now, &timeinfo);
            Serial.print(&timeinfo, ", UTC: \033[33m%Y-%m-%d %H:%M:%S\033[0m");

            Serial.printf(", HEAP: \033[33m%.2fkB\033[0m", float(ESP.getFreeHeap()) / 1024.0f);

            BatteryDriver* battery = this->battery.load();
            if (battery != nullptr) {
                Serial.printf(", battery: \033[33m%.2f V\033[0m", battery->getVoltage());
            }

            Serial.print(" ");
            Serial.flush();
            task.delayUntil(milliseconds(250));
        });
    }

    void registerBattery(BatteryDriver& battery) {
        this->battery = &battery;
    }

private:
    static String wifiStatus() {
        switch (WiFi.status()) {
            case WL_NO_SHIELD:
                return "NO SHIELD";
            case WL_IDLE_STATUS:
                return "IDLE STATUS";
            case WL_NO_SSID_AVAIL:
                return "NO SSID AVAIL";
            case WL_SCAN_COMPLETED:
                return "SCAN COMPLETED";
            case WL_CONNECTED:
                return "CONNECTED";
            case WL_CONNECT_FAILED:
                return "CONNECT FAILED";
            case WL_CONNECTION_LOST:
                return "CONNECTION LOST";
            case WL_DISCONNECTED:
                return "DISCONNECTED";
            default:
                return "UNKNOWN";
        }
    }

    int counter;
    std::atomic<BatteryDriver*> battery { nullptr };
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
    }

protected:
#ifdef FARMHUB_DEBUG
    ConsolePrinter consolePrinter;
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
