#pragma once

#include <map>
#include <optional>
#include <vector>

#include <Arduino.h>

#include <ArduinoJson.h>

namespace farmhub::devices {

class Pin {
public:
    static gpio_num_t registerPin(const String& name, gpio_num_t pin) {
        GPIO_NUMBERS[name] = pin;
        GPIO_NAMES[pin] = name;
        return pin;
    }

    static gpio_num_t numberOf(const String& name) {
        auto it = GPIO_NUMBERS.find(name);
        if (it != GPIO_NUMBERS.end()) {
            return it->second;
        }
        return GPIO_NUM_MAX;
    }

    static String nameOf(gpio_num_t pin) {
        auto it = GPIO_NAMES.find(pin);
        if (it == GPIO_NAMES.end()) {
            String name = "GPIO_NUM_" + String(pin);
            registerPin(name, pin);
            return name;
        } else {
            return it->second;
        }
    }

    static bool isRegistered(gpio_num_t pin) {
        return GPIO_NAMES.find(pin) != GPIO_NAMES.end();
    }

private:
    bool useName;

    static std::map<gpio_num_t, String> GPIO_NAMES;
    static std::map<String, gpio_num_t> GPIO_NUMBERS;
};

std::map<gpio_num_t, String> Pin::GPIO_NAMES;
std::map<String, gpio_num_t> Pin::GPIO_NUMBERS;

}    // namespace farmhub::devices

namespace ArduinoJson {

using farmhub::devices::Pin;

template <>
struct Converter<gpio_num_t> {
    static void toJson(const gpio_num_t& src, JsonVariant dst) {
        if (Pin::isRegistered(src)) {
            dst.set(Pin::nameOf(src));
        } else {
            dst.set(static_cast<int>(src));
        }
    }

    static gpio_num_t fromJson(JsonVariantConst src) {
        if (src.is<const char*>()) {
            return Pin::numberOf(src.as<const char*>());
        } else {
            return static_cast<gpio_num_t>(src.as<int>());
        }
    }

    static bool checkJson(JsonVariantConst src) {
        return src.is<const char*>() || src.is<int>();
    }
};

}    // namespace ArduinoJson
