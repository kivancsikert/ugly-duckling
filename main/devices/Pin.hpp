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

class InternalPin;
using InternalPinPtr = std::shared_ptr<InternalPin>;

class Pin {
public:
    static PinPtr byName(const String& name) {
        auto it = BY_NAME.find(name);
        if (it != BY_NAME.end()) {
            return it->second;
        }
        throw std::runtime_error(String("Unknown pin: " + name).c_str());
    }

    virtual void pinMode(uint8_t mode) const = 0;

    virtual void digitalWrite(uint8_t val) const = 0;

    virtual int digitalRead() const = 0;

    inline const String& getName() const {
        return name;
    }

    static void registerPin(const String& name, PinPtr pin) {
        BY_NAME[name] = pin;
    }

protected:
    Pin(const String& name)
        : name(name) {
    }

protected:
    const String name;

    static std::map<String, PinPtr> BY_NAME;
};

std::map<String, PinPtr> Pin::BY_NAME;

class InternalPin : public Pin {
public:
    static InternalPinPtr registerPin(const String& name, gpio_num_t gpio) {
        auto pin = std::make_shared<InternalPin>(name, gpio);
        INTERNAL_BY_GPIO[gpio] = pin;
        INTERNAL_BY_NAME[name] = pin;
        Pin::registerPin(name, pin);
        return pin;
    }

    static InternalPinPtr byName(const String& name) {
        auto it = INTERNAL_BY_NAME.find(name);
        if (it != INTERNAL_BY_NAME.end()) {
            return it->second;
        }
        throw std::runtime_error(String("Unknown internal pin: " + name).c_str());
    }

    static InternalPinPtr byGpio(gpio_num_t pin) {
        auto it = INTERNAL_BY_GPIO.find(pin);
        if (it == INTERNAL_BY_GPIO.end()) {
            String name = "GPIO_NUM_" + String(pin);
            return registerPin(name, pin);
        } else {
            return it->second;
        }
    }

    InternalPin(const String& name, gpio_num_t gpio)
        : Pin(name)
        , gpio(gpio) {
    }

    inline void pinMode(uint8_t mode) const override {
        ::pinMode(gpio, mode);
    }

    inline void digitalWrite(uint8_t val) const override {
        ::digitalWrite(gpio, val);
    }

    inline int digitalRead() const override {
        return ::digitalRead(gpio);
    }

    inline uint16_t analogRead() const {
        return ::analogRead(gpio);
    }

    inline gpio_num_t getGpio() const {
        return gpio;
    }

private:
    const gpio_num_t gpio;
    static std::map<String, InternalPinPtr> INTERNAL_BY_NAME;
    static std::map<gpio_num_t, InternalPinPtr> INTERNAL_BY_GPIO;
};

std::map<String, InternalPinPtr> InternalPin::INTERNAL_BY_NAME;
std::map<gpio_num_t, InternalPinPtr> InternalPin::INTERNAL_BY_GPIO;

}    // namespace farmhub::devices

namespace ArduinoJson {

using farmhub::devices::Pin;
using farmhub::devices::PinPtr;
using farmhub::devices::InternalPin;
using farmhub::devices::InternalPinPtr;

template <>
struct Converter<PinPtr> {
    static void toJson(const PinPtr& src, JsonVariant dst) {
        if (src == nullptr) {
            dst.set(nullptr);
        } else {
            dst.set(src->getName());
        }
    }

    static PinPtr fromJson(JsonVariantConst src) {
        if (src.is<const char*>()) {
            return Pin::byName(src.as<const char*>());
        } else {
            throw std::runtime_error(String("Invalid pin name: " + src.as<String>()).c_str());
        }
    }

    static bool checkJson(JsonVariantConst src) {
        return src.is<const char*>();
    }
};

template <>
struct Converter<InternalPinPtr> {
    static void toJson(const InternalPinPtr& src, JsonVariant dst) {
        if (src == nullptr) {
            dst.set(nullptr);
        } else if (src->getName().startsWith("GPIO_NUM_")) {
            dst.set(static_cast<int>(src->getGpio()));
        } else {
            dst.set(src->getName());
        }
    }

    static InternalPinPtr fromJson(JsonVariantConst src) {
        if (src.is<const char*>()) {
            return InternalPin::byName(src.as<const char*>());
        } else {
            return InternalPin::byGpio(static_cast<gpio_num_t>(src.as<int>()));
        }
    }

    static bool checkJson(JsonVariantConst src) {
        return src.is<const char*>() || src.is<int>();
    }
};

}    // namespace ArduinoJson
