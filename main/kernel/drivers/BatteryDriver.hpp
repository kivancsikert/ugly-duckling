#pragma once

#include <Arduino.h>

#include <devices/Pin.hpp>
#include <kernel/Log.hpp>
#include <kernel/Telemetry.hpp>

using farmhub::devices::PinPtr;

namespace farmhub::kernel::drivers {

class BatteryDriver : public TelemetryProvider {
public:
    virtual float getVoltage() = 0;

protected:
    virtual void populateTelemetry(JsonObject& json) override {
        json["voltage"] = getVoltage();
    }
};

class AnalogBatteryDriver
    : public BatteryDriver {
public:
    AnalogBatteryDriver(InternalPinPtr pin, float voltageDividerRatio)
        : pin(pin)
        , voltageDividerRatio(voltageDividerRatio) {
        Log.info("Initializing analog battery driver on pin %s",
            pin->getName().c_str());

        pin->pinMode(INPUT);
    }

    float getVoltage() {
        auto batteryLevel = pin->analogRead();
        return batteryLevel * 3.3 / 4096 * voltageDividerRatio;
    }

private:
    const InternalPinPtr pin;
    const float voltageDividerRatio;
};

}    // namespace farmhub::kernel::drivers
