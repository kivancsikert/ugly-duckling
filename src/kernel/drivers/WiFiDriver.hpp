#pragma once

#include <WiFi.h>
#include <WiFiManager.h>

#include <kernel/Task.hpp>

namespace farmhub { namespace kernel { namespace drivers {

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
    }

public:
    WiFiClient& getClient() {
        return wifiClient;
    }

private:
    WiFiManager wifiManager;
    WiFiClient wifiClient;
};

}}}
