#pragma once

#include <algorithm>
#include <string>

#include <Configuration.hpp>
#include <NetworkUtil.hpp>
#include <drivers/RtcDriver.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

namespace farmhub::devices {

class DeviceSettings : public ConfigurationSection {
public:
    explicit DeviceSettings(const std::string& defaultModel)
        : model(this, "model", defaultModel)
        , instance(this, "instance", getMacAddress()) {
    }

    Property<std::string> model;
    Property<std::string> id { this, "id", "UNIDENTIFIED" };
    Property<std::string> instance;
    Property<std::string> location { this, "location" };

    NamedConfigurationEntry<RtcDriver::Config> ntp { this, "ntp" };

    ArrayProperty<JsonAsString> peripherals { this, "peripherals" };

    Property<bool> sleepWhenIdle { this, "sleepWhenIdle", true };

    Property<seconds> publishInterval { this, "publishInterval", 1min };
    Property<Level> publishLogs { this, "publishLogs", Level::Info };

    std::string getHostname() const {
        std::string hostname = instance.get();
        std::ranges::replace(hostname, ':', '-');
        std::erase(hostname, '?');
        return hostname;
    }
};

}    // namespace farmhub::devices
