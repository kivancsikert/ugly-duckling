#pragma once

#include <algorithm>
#include <string>

#include <Configuration.hpp>
#include <MacAddress.hpp>
#include <drivers/RtcDriver.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

namespace farmhub::devices {

struct DeviceSettings : ConfigurationSection {
    explicit DeviceSettings(const std::string& defaultModel)
        : model(this, "model", defaultModel)
        , instance(this, "instance", getMacAddress()) {
    }

    Property<std::string> model;
    Property<std::string> instance;
    Property<std::string> location { this, "location" };

    NamedConfigurationEntry<RtcDriver::Config> ntp { this, "ntp" };

    ArrayProperty<JsonAsString> peripherals { this, "peripherals" };
    ArrayProperty<JsonAsString> functions { this, "functions" };

    Property<bool> sleepWhenIdle { this, "sleepWhenIdle", true };

    /**
     * @brief How often to publish telemetry.
     */
    Property<seconds> publishInterval { this, "publishInterval", 5min };
    Property<Level> publishLogs { this, "publishLogs",
#ifdef FARMHUB_DEBUG
        Level::Verbose
#else
        Level::Info
#endif
    };

    /**
     * @brief How long without successfully published telemetry before the watchdog times out and reboots the device.
     */
    Property<seconds> watchdogTimeout { this, "watchdogTimeout", 15min };

    std::string getHostname() const {
        std::string hostname = instance.get();
        std::ranges::replace(hostname, ':', '-');
        std::erase(hostname, '?');
        return hostname;
    }
};

}    // namespace farmhub::devices
