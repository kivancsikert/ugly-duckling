#pragma once

#include <ArduinoJson.h>

#include "IPeripheral.hpp"
#include "TargetState.hpp"

namespace farmhub::peripherals::api {

enum class DoorState : int8_t {
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
    /**
     * @brief Transition the door to a new state.
     *
     * @param target The target state to transition to. When unspecified, it stays in its current state.
     *                When current state is unspecified (`None`), transitions to `Closed`.
     * @return true if the state was changed, false if it was already in the target state.
     */
    virtual bool transitionTo(std::optional<TargetState> target) = 0;

    /**
     * @brief Get the current state of the door.
     */
    virtual DoorState getState() = 0;
};

}    // namespace farmhub::peripherals::api
