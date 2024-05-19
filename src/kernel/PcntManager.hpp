#pragma once

#include <limits>

#include <driver/pulse_cnt.h>

#include <Arduino.h>

#include <kernel/Log.hpp>

namespace farmhub::kernel {

// TODO Figure out what to do with low/high speed modes
//      See https://docs.espressif.com/projects/esp-idf/en/release-v4.2/esp32/api-reference/peripherals/ledc.html#ledc-high-and-low-speed-mode

// TODO Limit number of channels available
struct PcntUnit {
    PcntUnit(pcnt_unit_handle_t unit, gpio_num_t pin)
        : unit(unit)
        , pin(pin) {
    }

    PcntUnit()
        : PcntUnit(nullptr, GPIO_NUM_MAX) {
    }

    PcntUnit(const PcntUnit& other)
        : PcntUnit(other.unit, other.pin) {
    }

    PcntUnit& operator=(const PcntUnit& other) {
        if (this != &other) {    // Self-assignment check
            unit = other.unit;
            pin = other.pin;
        }
        return *this;
    }

    int getCount() const {
        int count;
        pcnt_unit_get_count(unit, &count);
        return count;
    }

    void clear() {
        pcnt_unit_clear_count(unit);
    }

    int getAndClearCount() {
        int count = getCount();
        if (count != 0) {
            clear();
        }
        return count;
    }

    gpio_num_t getPin() const {
        return pin;
    }

private:
    pcnt_unit_handle_t unit;
    gpio_num_t pin;
};

class PcntManager {
public:
    PcntUnit registerUnit(gpio_num_t pin) {
        pcnt_unit_config_t unitConfig = {
            .low_limit = std::numeric_limits<int16_t>::min(),
            .high_limit = std::numeric_limits<int16_t>::max(),
            // TODO Is this the right priority to set?
            .intr_priority = 0,
            .flags = {

            },
        };

        pcnt_unit_handle_t unit = nullptr;
        ESP_ERROR_CHECK(pcnt_new_unit(&unitConfig, &unit));

        pcnt_glitch_filter_config_t filterConfig = {
            .max_glitch_ns = 1023,
        };
        ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(unit, &filterConfig));

        // TODO Should we use level or edge here? Shall the method receive both?
        pcnt_chan_config_t chanConfig = {
            .edge_gpio_num = pin,
            .level_gpio_num = -1,
            .flags = {},
        };
        pcnt_channel_handle_t channel = nullptr;
        ESP_ERROR_CHECK(pcnt_new_channel(unit, &chanConfig, &channel));

        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD));

        pcnt_unit_clear_count(unit);

        Log.debug("Registered PCNT unit %d on pin %d",
            unit, pin);
        return PcntUnit(unit, pin);
    }

private:
    uint8_t nextUnit = 0;
};

}    // namespace farmhub::kernel
