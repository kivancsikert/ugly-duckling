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

    EventGroupHandle_t eventGroup { xEventGroupCreate() };
    WiFiDriver wifiDriver;
    NtpDriver ntpDriver { eventGroup, NTP_SYNCED_BIT };
    MqttDriver mqttDriver { wifiDriver };

    static const int NTP_SYNCED_BIT = 1;
};

}}    // namespace farmhub::device
