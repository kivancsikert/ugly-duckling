#pragma once

#include <Arduino.h>
#include <ArduinoOTA.h>

#include <kernel/drivers/WiFiDriver.hpp>

#include <kernel/Task.hpp>

namespace farmhub { namespace kernel { namespace drivers {

class OtaDriver {

public:
    OtaDriver(State& networkReady, const String& hostname) {
        Task::run("OTA", [&networkReady, hostname](Task& task) {
            networkReady.awaitSet();

            ArduinoOTA.setHostname(hostname.c_str());

            bool updating = false;
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

            // TODO Update wake time before loop to avoid task "missing" first deadline

            while (true) {
                ArduinoOTA.handle();
                task.delayUntil(updating
                        ? seconds::zero()
                        : seconds(1));
            }
        });
    }
};

}}}    // namespace farmhub::kernel::drivers
