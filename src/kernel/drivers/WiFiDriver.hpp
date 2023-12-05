#pragma once

#include <WiFi.h>

#include <WiFiManager.h>

#include <kernel/Task.hpp>

namespace farmhub { namespace kernel { namespace drivers {

class WiFiDriver
    : Task,
      public EventSource {
public:
    WiFiDriver(EventGroupHandle_t eventGroup, int eventBit)
        : EventSource(eventGroup, eventBit)
        , Task("Connect to WiFi") {
    }

protected:
    void run() override {
        // Explicitly set mode, ESP defaults to STA+AP
        WiFi.mode(WIFI_STA);

        // TODO Should we clear the event bit when disconnected?
        WiFi.onEvent(
            [this](WiFiEvent_t event, WiFiEventInfo_t info) {
                Serial.println("WiFi: connected");
                emitEventFromISR();
            },
            ARDUINO_EVENT_WIFI_STA_CONNECTED);

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

}}}    // namespace farmhub::kernel::drivers
