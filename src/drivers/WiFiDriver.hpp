#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <WiFi.h>
#include <WiFiManager.h>

#include <Task.hpp>

namespace farmhub { namespace device { namespace drivers {

class WiFiDriver : Task {
public:
    WiFiDriver()
        : Task("Connect to WiFi") {
    }

protected:
    void run() override {
        // Explicitly set mode, ESP defaults to STA+AP
        WiFi.mode(WIFI_STA);

        wifiManager.autoConnect("AutoConnectAP");

        // TODO Use time server from MDNS
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    }

public:
    WiFiClient& getClient() {
        return wifiClient;
    }

private:
    WiFiManager wifiManager;
    WiFiClient wifiClient;
};

}}}    // namespace farmhub::device::drivers