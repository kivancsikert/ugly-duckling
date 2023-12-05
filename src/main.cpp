#include <Arduino.h>

#include <kernel/Application.hpp>
#include <kernel/FileSystem.hpp>
#include <kernel/Task.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

class BlinkLedTask : public LoopTask {
public:
    BlinkLedTask(gpio_num_t ledPin, int interval)
        : LoopTask("Blink LED", 1000, 1)
        , ledPin(ledPin)
        , interval(interval) {
        pinMode(ledPin, OUTPUT);
    }

protected:
    void loop() override {
        digitalWrite(ledPin, HIGH);
        delayUntil(interval);
        digitalWrite(ledPin, LOW);
        delayUntil(interval);
    }

private:
    const gpio_num_t ledPin;
    const int interval;
};

class ConsolePrinter : IntermittentLoopTask {
public:
    ConsolePrinter()
        : IntermittentLoopTask("Console printer", 32768, 1) {
    }

protected:
    int loopAndDelay() override {
        Serial.print("\033[1G\033[0K");

        counter = (counter + 1) % spinner.length();
        Serial.print("[" + spinner.substring(counter, counter + 1) + "] ");

        Serial.print("WIFI: \033[33m" + wifiStatus() + "\033[0m");
        Serial.print(", uptime: \033[33m" + String(millis()) + "\033[0m ms");

        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        Serial.printf(", now: \033[33m%d\033[0m", now);
        Serial.print(&timeinfo, ", UTC: \033[33m%A, %B %d %Y %H:%M:%S\033[0m");

        Serial.print(" ");
        return 100;
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
    const String spinner { "-\\|/" };
};

class BlinkerDeviceConfiguration
    : public DeviceConfiguration {
public:
    BlinkerDeviceConfiguration(FileSystem& fs)
        : DeviceConfiguration(fs, "mk1") {
    }
};

class BlinkerApplication : public Application {

public:
    BlinkerApplication(BlinkerDeviceConfiguration& deviceConfig)
        : Application(deviceConfig) {
    }

private:
    BlinkLedTask blinkLedTask1 { GPIO_NUM_2, 2500 };
    BlinkLedTask blinkLedTask2 { GPIO_NUM_4, 1500 };
    ConsolePrinter consolePrinter;
};

BlinkerDeviceConfiguration* deviceConfig;
BlinkerApplication* application;

void setup() {
    Serial.begin(115200);
    Serial.println("Starting up...");

    deviceConfig = new BlinkerDeviceConfiguration(FileSystem::instance());
    application = new BlinkerApplication(*deviceConfig);
}

void loop() {
}
