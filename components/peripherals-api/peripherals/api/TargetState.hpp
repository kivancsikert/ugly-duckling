#pragma once

#include <optional>

#include <ArduinoJson.h>

namespace farmhub::peripherals::api {

enum class TargetState : int8_t {
    Closed = -1,
    Open = 1
};

inline static const char* toString(std::optional<TargetState> state) {
    if (!state.has_value()) {
        return "None";
    }
    switch (state.value()) {
        case TargetState::Closed:
            return "Closed";
        case TargetState::Open:
            return "Open";
        default:
            return "INVALID";
    }
}

}    // namespace farmhub::peripherals::api

namespace ArduinoJson {

using farmhub::peripherals::api::TargetState;

template <>
struct Converter<TargetState> {
    static void toJson(TargetState src, JsonVariant dst) {
        switch (src) {
            case TargetState::Closed:
                dst.set("Closed");
                break;
            case TargetState::Open:
                dst.set("Open");
                break;
            default:
                throw std::invalid_argument("Invalid TargetState " + std::to_string(static_cast<int>(src)));
                break;
        }
    }

    static farmhub::peripherals::api::TargetState fromJson(JsonVariantConst src) {
        const char* str = src.as<const char*>();
        if (strcmp(str, "Closed") == 0) {
            return farmhub::peripherals::api::TargetState::Closed;
        }
        if (strcmp(str, "Open") == 0) {
            return farmhub::peripherals::api::TargetState::Open;
        }
        throw std::invalid_argument("Invalid TargetState '" + std::string(str) + "'");
    }

    static bool checkJson(JsonVariantConst src) {
        return src.is<const char*>();
    }
};
}    // namespace ArduinoJson
