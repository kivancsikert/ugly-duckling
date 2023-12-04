#pragma once

#include <freertos/FreeRTOS.h>

#include <drivers/MdnsDriver.hpp>
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
    MdnsDriver mdnsDriver { hostname, "FarmHub", "0.1", eventGroup, MDNS_CONFIGURED_BIT };
    MqttDriver mqttDriver { mdnsDriver, wifiDriver };

    static const int NTP_SYNCED_BIT = 1;
    static const int MDNS_CONFIGURED_BIT = 2;
};

}}    // namespace farmhub::device
