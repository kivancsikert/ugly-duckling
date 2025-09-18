#pragma once

#include <ArduinoJson.h>

#include "IPeripheral.hpp"
#include "TargetState.hpp"

namespace farmhub::peripherals::api {

enum class ValveState : int8_t {
    Closed = -1,
    None = 0,
    Open = 1
};

inline static const char* toString(ValveState state) {
    switch (state) {
        case ValveState::Closed:
            return "Closed";
        case ValveState::Open:
            return "Open";
        case ValveState::None:
            return "None";
        default:
            return "INVALID";
    }
}

struct IValve : virtual IPeripheral {
    /**
     * @brief Transition the valve to a new state.
     *
     * @param target The target state to transition to.
     *        If not specified, the valve will transition to its default state.
     * @return true if the state was changed, false if it was already in the target state.
     */
    virtual bool transitionTo(std::optional<TargetState> target) = 0;

    /**
     * @brief Get the current state of the valve.
     */
    virtual ValveState getState() const = 0;
};

}    // namespace farmhub::peripherals::api

namespace ArduinoJson {

using farmhub::peripherals::api::ValveState;

template <>
struct Converter<ValveState> {
    static void toJson(ValveState src, JsonVariant dst) {
        switch (src) {
            case ValveState::Closed:
                dst.set("Closed");
                break;
            case ValveState::Open:
                dst.set("Open");
                break;
            default:
                throw std::invalid_argument("Invalid ValveState");
                break;
        }
    }

    static farmhub::peripherals::api::ValveState fromJson(JsonVariantConst src) {
        const char* str = src.as<const char*>();
        if (strcmp(str, "Closed") == 0) {
            return farmhub::peripherals::api::ValveState::Closed;
        }
        if (strcmp(str, "Open") == 0) {
            return farmhub::peripherals::api::ValveState::Open;
        }
        throw std::invalid_argument("Invalid ValveState");
    }

    static bool checkJson(JsonVariantConst src) {
        return src.is<const char*>();
    }
};

}    // namespace ArduinoJson
