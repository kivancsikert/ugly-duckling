#pragma once

#include <string>

#include <kernel/Configuration.hpp>
#include <kernel/NetworkUtil.hpp>
#include <kernel/drivers/RtcDriver.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

namespace farmhub::devices {

class DeviceConfiguration : public ConfigurationSection {
public:
    DeviceConfiguration(const std::string& defaultModel)
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

    virtual const std::string getHostname() {
        std::string hostname = instance.get();
        std::replace(hostname.begin(), hostname.end(), ':', '-');
        std::erase(hostname, '?');
        return hostname;
    }
};

}    // namespace farmhub::devices
