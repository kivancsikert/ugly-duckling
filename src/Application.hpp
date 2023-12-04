#pragma once

#include <freertos/FreeRTOS.h>

#include <drivers/MqttDriver.hpp>
#include <drivers/NtpDriver.hpp>
#include <drivers/WiFiDriver.hpp>

namespace farmhub { namespace device {

using namespace farmhub::device::drivers;

class Application {
public:
    Application(const String& hostname)
        : hostname(hostname) {
    }

private:
    const String hostname;

    WiFiDriver wifiDriver;
    NtpDriver ntpDriver;
    MqttDriver mqttDriver { wifiDriver };
};

}}    // namespace farmhub::device
