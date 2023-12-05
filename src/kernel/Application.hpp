#pragma once

#include <freertos/FreeRTOS.h>

#include <kernel/drivers/MdnsDriver.hpp>
#include <kernel/drivers/MqttDriver.hpp>
#include <kernel/drivers/NtpDriver.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

namespace farmhub { namespace kernel {

using namespace farmhub::kernel::drivers;

class Application {
public:
    Application(const String& hostname, const String& version)
        : hostname(hostname),
        version(version) {
    }

private:
    const String hostname;
    const String version;

    EventGroupHandle_t eventGroup { xEventGroupCreate() };
    WiFiDriver wifi;
    MdnsDriver mdns { hostname, "ugly-duckling", version, eventGroup, MDNS_CONFIGURED_BIT };
    NtpDriver ntp { mdns, eventGroup, NTP_SYNCED_BIT };
    MqttDriver mqtt { mdns, wifi };

    static const int NTP_SYNCED_BIT = 1;
    static const int MDNS_CONFIGURED_BIT = 2;
};

}}
