#include <Arduino.h>

#include <kernel/Application.hpp>
#include <kernel/FileSystem.hpp>
#include <kernel/Task.hpp>
#include <kernel/drivers/BatteryDriver.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

class ConsolePrinter {
public:
    ConsolePrinter(BatteryDriver& batteryDriver) {
        static const String spinner = "|/-\\";
        Task::loop("ConsolePrinter", 32 * 1024, 1, [this, &batteryDriver](Task& task) {
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

            Serial.printf(", battery: \033[33m%.2f V\033[0m", batteryDriver.getVoltage());

            Serial.print(" ");
            Serial.flush();
            task.delayUntil(milliseconds(100));
        });
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
};

class SampleDeviceConfiguration
    : public DeviceConfiguration {
public:
    SampleDeviceConfiguration()
        : DeviceConfiguration("mk1") {
    }
};

class SampleDevice {

public:
    SampleDevice()
        : application(GPIO_NUM_2) {

        application.registerTelemetryProvider("battery", batteryDriver);
    }

private:
    BatteryDriver batteryDriver { GPIO_NUM_1, 1.242424 };
    ConsolePrinter consolePrinter { batteryDriver };
    Application<SampleDeviceConfiguration> application;

    LedDriver secondaryStatus { "status-2", GPIO_NUM_4, { milliseconds(250), milliseconds(-250) } };
};

SampleDevice* device;

void setup() {
    Serial.begin(115200);
    Serial.println("Starting up...");

    device = new SampleDevice();
}

void loop() {
}
