#pragma once

#include <ArduinoJson.h>

#include "IPeripheral.hpp"

namespace farmhub::peripherals::api {

enum class DoorState : int8_t {
    Initialized = -2,
    Closed = -1,
    None = 0,
    Open = 1
};

bool convertToJson(const DoorState& src, JsonVariant dst) {
    return dst.set(static_cast<int>(src));
}
void convertFromJson(JsonVariantConst src, DoorState& dst) {
    dst = static_cast<DoorState>(src.as<int>());
}

struct IDoor : virtual IPeripheral {
    virtual void setTarget(DoorState target) = 0;
    virtual DoorState getTarget() = 0;
    virtual DoorState getState() = 0;
};

}    // namespace farmhub::peripherals::api
