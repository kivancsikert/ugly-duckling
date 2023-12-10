#pragma once

#include <kernel/Application.hpp>
#include <kernel/FileSystem.hpp>
#include <kernel/drivers/BatteryDriver.hpp>

using namespace farmhub::kernel;

namespace farmhub { namespace devices {

class DeviceConfig
    : public DeviceConfiguration {
public:
    DeviceConfig()
        : DeviceConfiguration("mk6") {
    }
};

class UglyDucklingMk6 {
public:
    UglyDucklingMk6() {
        application.registerTelemetryProvider("battery", batteryDriver);
    }

    BatteryDriver& getBatteryDriver() {
        return batteryDriver;
    }

    LedDriver& getStatusLed() {
        return status;
    }

    LedDriver& getSecondaryStatusLed() {
        return secondaryStatus;
    }

private:
    LedDriver status { "status", GPIO_NUM_2 };
    LedDriver secondaryStatus { "status-2", GPIO_NUM_4 };
    Application<DeviceConfig> application { status };

    BatteryDriver batteryDriver { GPIO_NUM_1, 1.242424 };
};

}}    // namespace farmhub::devices
