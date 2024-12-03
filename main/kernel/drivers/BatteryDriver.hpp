#pragma once

#include <Arduino.h>

#include <kernel/Log.hpp>
#include <kernel/Pin.hpp>
#include <kernel/Telemetry.hpp>

using farmhub::kernel::PinPtr;

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
        LOGI("Initializing analog battery driver on pin %s",
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
