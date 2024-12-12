#pragma once

#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>
#include <hal/adc_types.h>

#include <Arduino.h>

#include <ArduinoJson.h>

namespace farmhub::kernel {

class Pin;
using PinPtr = std::shared_ptr<Pin>;

class InternalPin;
using InternalPinPtr = std::shared_ptr<InternalPin>;

/**
 * @brief A GPIO pin abstraction that allows digital reads and writes.
 *
 * @details This can be implemented by internal pins (GPIO pins of the MCU) or external pins provided by external peripherals.
 */
class Pin {
public:
    static PinPtr byName(const String& name) {
        auto it = BY_NAME.find(name);
        if (it != BY_NAME.end()) {
            return it->second;
        }
        throw std::runtime_error(String("Unknown pin: " + name).c_str());
    }

    enum class Mode {
        Output,
        Input,
        InputPullUp,
        InputPullDown,
    };

    virtual void pinMode(Mode mode) const = 0;

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

/**
 * @brief An internal GPIO pin of the MCU. These pins can do analog reads as well, and can expose the GPIO number.
 */
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

    void pinMode(Mode mode) const override {
        gpio_config_t conf = {
            .pin_bit_mask = (1ULL << gpio),
            .mode = mode == Mode::Output ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT,
            .pull_up_en = mode == Mode::InputPullUp ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = mode == Mode::InputPullDown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&conf));
    }

    inline void digitalWrite(uint8_t val) const override {
        gpio_set_level(gpio, val);
    }

    inline int digitalRead() const override {
        return gpio_get_level(gpio);
    }

    inline gpio_num_t getGpio() const {
        return gpio;
    }

private:
    const gpio_num_t gpio;
    static std::map<String, InternalPinPtr> INTERNAL_BY_NAME;
    static std::map<gpio_num_t, InternalPinPtr> INTERNAL_BY_GPIO;
};

class AnalogPin {
public:
    AnalogPin(const InternalPinPtr pin)
        : pin(pin) {
        adc_unit_t unit;
        ESP_ERROR_CHECK(adc_oneshot_io_to_channel(pin->getGpio(), &unit, &channel));

        handle = getUnitHandle(unit);

        adc_oneshot_chan_cfg_t config = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(handle, channel, &config));
    }

    ~AnalogPin() {
        ESP_ERROR_CHECK(adc_oneshot_del_unit(handle));
    }

    int analogRead() const {
        int value;
        ESP_ERROR_CHECK(adc_oneshot_read(handle, channel, &value));
        return value;
    }

    const String& getName() const {
        return pin->getName();
    }

private:
    static adc_oneshot_unit_handle_t getUnitHandle(adc_unit_t unit) {
        adc_oneshot_unit_handle_t handle = ANALOG_UNITS[unit];
        if (handle == nullptr) {
            adc_oneshot_unit_init_cfg_t config = {
                .unit_id = unit,
                .ulp_mode = ADC_ULP_MODE_DISABLE,
            };
            ESP_ERROR_CHECK(adc_oneshot_new_unit(&config, &handle));
            ANALOG_UNITS[unit] = handle;
        }
        return handle;
    }

    static std::vector<adc_oneshot_unit_handle_t> ANALOG_UNITS;

    const InternalPinPtr pin;
    adc_oneshot_unit_handle_t handle;
    adc_channel_t channel;
};

std::map<String, InternalPinPtr> InternalPin::INTERNAL_BY_NAME;
std::map<gpio_num_t, InternalPinPtr> InternalPin::INTERNAL_BY_GPIO;
std::vector<adc_oneshot_unit_handle_t> AnalogPin::ANALOG_UNITS { 2 };

}    // namespace farmhub::kernel

namespace ArduinoJson {

using farmhub::kernel::InternalPin;
using farmhub::kernel::InternalPinPtr;
using farmhub::kernel::Pin;
using farmhub::kernel::PinPtr;

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
