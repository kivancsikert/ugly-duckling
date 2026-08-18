#pragma once

#include <Log.hpp>

#include <ArduinoJson.h>

namespace ArduinoJson {

using cornucopia::ugly_duckling::kernel::Level;

template <>
struct Converter<Level> {
    static bool toJson(const Level& src, JsonVariant dst) {
        return dst.set(static_cast<int>(src));
    }

    static Level fromJson(JsonVariantConst src) {
        return static_cast<Level>(src.as<std::uint8_t>());
    }

    static bool checkJson(JsonVariantConst src) {
        return src.is<int>();
    }
};

}    // namespace ArduinoJson
