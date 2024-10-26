#pragma once

#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <Arduino.h>

#include <ArduinoJson.h>

namespace farmhub::devices {

class Pin;
using PinPtr = std::shared_ptr<Pin>;

class Pin {
public:
    static PinPtr registerPin(const String& name, gpio_num_t gpio) {
        auto pin = std::make_shared<Pin>(name, gpio);
        BY_GPIO[gpio] = pin;
        BY_NAME[name] = pin;
        return pin;
    }

    static PinPtr byName(const String& name) {
        auto it = BY_NAME.find(name);
        if (it != BY_NAME.end()) {
            return it->second;
        }
        throw std::runtime_error(String("Unknown pin: " + name).c_str());
    }

    static PinPtr byGpio(gpio_num_t pin) {
        auto it = BY_GPIO.find(pin);
        if (it == BY_GPIO.end()) {
            String name = "GPIO_NUM_" + String(pin);
            return registerPin(name, pin);
        } else {
            return it->second;
        }
    }

    Pin(const String& name, gpio_num_t gpio)
        : name(name)
        , gpio(gpio) {
    }

    inline void pinMode(uint8_t mode) const {
        ::pinMode(gpio, mode);
    }

    inline void digitalWrite(uint8_t val) const {
        ::digitalWrite(gpio, val);
    }

    inline int digitalRead() const {
        return ::digitalRead(gpio);
    }

    inline uint16_t analogRead() const {
        return ::analogRead(gpio);
    }

    inline const String& getName() const {
        return name;
    }

    inline gpio_num_t getGpio() const {
        return gpio;
    }

private:
    const String name;
    const gpio_num_t gpio;

    static std::map<gpio_num_t, PinPtr> BY_GPIO;
    static std::map<String, PinPtr> BY_NAME;
};

std::map<gpio_num_t, PinPtr> Pin::BY_GPIO;
std::map<String, PinPtr> Pin::BY_NAME;

}    // namespace farmhub::devices

namespace ArduinoJson {

using farmhub::devices::Pin;
using farmhub::devices::PinPtr;

template <>
struct Converter<PinPtr> {
    static void toJson(const PinPtr& src, JsonVariant dst) {
        if (src == nullptr) {
            dst.set(nullptr);
        } else if (src->getName().startsWith("GPIO_NUM_")) {
            dst.set(static_cast<int>(src->getGpio()));
        } else {
            dst.set(src->getName());
        }
    }

    static PinPtr fromJson(JsonVariantConst src) {
        if (src.is<const char*>()) {
            return Pin::byName(src.as<const char*>());
        } else {
            return Pin::byGpio(static_cast<gpio_num_t>(src.as<int>()));
        }
    }

    static bool checkJson(JsonVariantConst src) {
        return src.is<const char*>() || src.is<int>();
    }
};

}    // namespace ArduinoJson
