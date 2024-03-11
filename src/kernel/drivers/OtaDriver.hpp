#pragma once

#include <chrono>

#include <Arduino.h>
#include <ArduinoOTA.h>

#include <ArduinoLog.h>

#include <kernel/drivers/WiFiDriver.hpp>

#include <kernel/Task.hpp>

using namespace std::chrono_literals;

namespace farmhub::kernel::drivers {

class OtaDriver {

public:
    OtaDriver(State& networkReady, const String& hostname) {
        Task::run("ota:init", 3072, [&networkReady, hostname, this](Task& task) {
            networkReady.awaitSet();

            ArduinoOTA.setHostname(hostname.c_str());

            ArduinoOTA.onStart([&]() {
                Log.infoln("OTA update started");
            });
            ArduinoOTA.onEnd([&]() {
                Log.infoln("OTA update finished");
            });
            ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
                Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
            });
            ArduinoOTA.onError([&](ota_error_t error) {
                Log.errorln("Web socket error[%u]", error);
                if (error == OTA_AUTH_ERROR) {
                    Log.errorln("Auth Failed");
                } else if (error == OTA_BEGIN_ERROR) {
                    Log.errorln("Begin Failed");
                } else if (error == OTA_CONNECT_ERROR) {
                    Log.errorln("Connect Failed");
                } else if (error == OTA_RECEIVE_ERROR) {
                    Log.errorln("Receive Failed");
                } else if (error == OTA_END_ERROR) {
                    Log.errorln("End Failed");
                } else {
                    Log.errorln("Other error");
                }
            });
            ArduinoOTA.begin();

            Log.infoln("OTA initialized on hostname %s",
                hostname.c_str());

            Task::loop("ota", 3072, [this](Task& task) {
                ArduinoOTA.handle();
                task.delayUntil(1s);
            });
        });
    }
};

}    // namespace farmhub::kernel::drivers
