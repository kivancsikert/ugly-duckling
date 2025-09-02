#pragma once

#include "IPeripheral.hpp"

namespace farmhub::peripherals::api {

enum class TargetState : int8_t {
    CLOSED = -1,
    OPEN = 1
};

inline static const char* toString(std::optional<TargetState> state) {
    if (!state.has_value()) {
        return "NONE";
    }
    switch (state.value()) {
        case TargetState::CLOSED:
            return "CLOSED";
        case TargetState::OPEN:
            return "OPEN";
        default:
            return "INVALID";
    }
}

enum class ValveState : int8_t {
    CLOSED = -1,
    NONE = 0,
    OPEN = 1
};

inline static const char* toString(ValveState state) {
    switch (state) {
        case ValveState::CLOSED:
            return "CLOSED";
        case ValveState::OPEN:
            return "OPEN";
        case ValveState::NONE:
            return "NONE";
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

using farmhub::peripherals::api::TargetState;

template <>
struct Converter<TargetState> {
    static void toJson(TargetState src, JsonVariant dst) {
        switch (src) {
            case TargetState::CLOSED:
                dst.set("CLOSED");
                break;
            case TargetState::OPEN:
                dst.set("OPEN");
                break;
            default:
                throw std::invalid_argument("Invalid TargetState");
                break;
        }
    }

    static farmhub::peripherals::api::TargetState fromJson(JsonVariantConst src) {
        const char* str = src.as<const char*>();
        if (strcmp(str, "CLOSED") == 0) {
            return farmhub::peripherals::api::TargetState::CLOSED;
        }
        if (strcmp(str, "OPEN") == 0) {
            return farmhub::peripherals::api::TargetState::OPEN;
        }
        throw std::invalid_argument("Invalid TargetState");
    }

    static bool checkJson(JsonVariantConst src) {
        return src.is<const char*>();
    }
};

using farmhub::peripherals::api::ValveState;

template <>
struct Converter<ValveState> {
    static void toJson(ValveState src, JsonVariant dst) {
        switch (src) {
            case ValveState::CLOSED:
                dst.set("CLOSED");
                break;
            case ValveState::OPEN:
                dst.set("OPEN");
                break;
            default:
                throw std::invalid_argument("Invalid ValveState");
                break;
        }
    }

    static farmhub::peripherals::api::ValveState fromJson(JsonVariantConst src) {
        const char* str = src.as<const char*>();
        if (strcmp(str, "CLOSED") == 0) {
            return farmhub::peripherals::api::ValveState::CLOSED;
        }
        if (strcmp(str, "OPEN") == 0) {
            return farmhub::peripherals::api::ValveState::OPEN;
        }
        throw std::invalid_argument("Invalid ValveState");
    }

    static bool checkJson(JsonVariantConst src) {
        return src.is<const char*>();
    }
};

}    // namespace ArduinoJson
