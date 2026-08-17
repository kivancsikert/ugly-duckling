#pragma once

#include <algorithm>
#include <string>

#include <MacAddress.hpp>
#include <drivers/RtcDriver.hpp>
#include <mqtt/MqttDriver.hpp>

using namespace cornucopia::ugly_duckling::kernel;

/**
 * @brief Network configuration: MQTT broker settings, NTP, plus device instance and location.
 * Stored under the "network-config" key in NVS.
 */
struct NetworkConfig : MqttDriver::Config {
    Property<std::string> instance { this, "instance", getMacAddress() };
    Property<std::string> location { this, "location" };
    NamedConfigurationEntry<RtcDriver::Config> ntp { this, "ntp" };

    std::string getHostname() const {
        std::string hostname = instance.get();
        std::ranges::replace(hostname, ':', '-');
        std::erase(hostname, '?');
        return hostname;
    }
};
