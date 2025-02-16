#pragma once

#include <ArduinoJson.h>

#include <Log.hpp>

namespace farmhub::kernel {

bool convertToJson(const Level& src, JsonVariant dst) {
    return dst.set(static_cast<int>(src));
}
void convertFromJson(JsonVariantConst src, Level& dst) {
    dst = static_cast<Level>(src.as<int>());
}

}    // namespace farmhub::kernel
