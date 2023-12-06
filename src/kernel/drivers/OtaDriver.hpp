#pragma once

#include <Arduino.h>
#include <ArduinoOTA.h>

#include <kernel/drivers/WiFiDriver.hpp>

#include <kernel/Task.hpp>

namespace farmhub { namespace kernel { namespace drivers {

class OtaDriver
    : public IntermittentLoopTask {

public:
    OtaDriver(Event& networkReady, const String& hostname)
        : IntermittentLoopTask("OTA")
        , networkReady(networkReady)
        , hostname(hostname) {
    }

protected:
    void setup() override {
        networkReady.await();

        ArduinoOTA.setHostname(hostname.c_str());
        ArduinoOTA.onStart([&]() {
            Serial.println("Start");
            updating = true;
        });
        ArduinoOTA.onEnd([&]() {
            Serial.println("\nEnd");
            updating = false;
        });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        });
        ArduinoOTA.onError([&](ota_error_t error) {
            Serial.printf("Web socket error[%u]\n", error);
            if (error == OTA_AUTH_ERROR) {
                Serial.println("Auth Failed");
            } else if (error == OTA_BEGIN_ERROR) {
                Serial.println("Begin Failed");
            } else if (error == OTA_CONNECT_ERROR) {
                Serial.println("Connect Failed");
            } else if (error == OTA_RECEIVE_ERROR) {
                Serial.println("Receive Failed");
            } else if (error == OTA_END_ERROR) {
                Serial.println("End Failed");
            } else {
                Serial.println("Other error");
            }
            updating = false;
        });
        ArduinoOTA.begin();

        Serial.println("OTA initialized on hostname " + hostname);
    }

    milliseconds loopAndDelay() override {
        ArduinoOTA.handle();
        return updating
            ? seconds::zero()
            : seconds(1);
    }

private:
    Event& networkReady;
    const String hostname;
    bool updating = false;
};

}}}    // namespace farmhub::kernel::drivers
