#pragma once

#include <chrono>

#include <Arduino.h>
#include <ArduinoOTA.h>

#include <kernel/Log.hpp>
#include <kernel/Task.hpp>

#include <kernel/drivers/WiFiDriver.hpp>

using namespace std::chrono_literals;

namespace farmhub::kernel::drivers {

class OtaDriver {

public:
    OtaDriver(State& networkReady, const String& hostname) {
        Task::run("ota:init", 3072, [&networkReady, hostname, this](Task& task) {
            networkReady.awaitSet();

            ArduinoOTA.setHostname(hostname.c_str());

            ArduinoOTA.onStart([&]() {
                Log.info("OTA update started");
            });
            ArduinoOTA.onEnd([&]() {
                Log.info("OTA update finished");
            });
            ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
                Log.printfToSerial("Progress: %u%%\r", (progress / (total / 100)));
            });
            ArduinoOTA.onError([&](ota_error_t error) {
                Log.error("Web socket error[%u]", error);
                if (error == OTA_AUTH_ERROR) {
                    Log.error("Auth Failed");
                } else if (error == OTA_BEGIN_ERROR) {
                    Log.error("Begin Failed");
                } else if (error == OTA_CONNECT_ERROR) {
                    Log.error("Connect Failed");
                } else if (error == OTA_RECEIVE_ERROR) {
                    Log.error("Receive Failed");
                } else if (error == OTA_END_ERROR) {
                    Log.error("End Failed");
                } else {
                    Log.error("Other error");
                }
            });
            ArduinoOTA.begin();

            Log.info("OTA initialized on hostname %s",
                hostname.c_str());

            Task::loop("ota", 3072, [this](Task& task) {
                ArduinoOTA.handle();
                task.delayUntil(1s);
            });
        });
    }
};

}    // namespace farmhub::kernel::drivers
