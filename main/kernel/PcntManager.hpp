#pragma once

#include <limits>

#include <driver/pulse_cnt.h>

#include <Arduino.h>

namespace farmhub::kernel {

// TODO Figure out what to do with low/high speed modes
//      See https://docs.espressif.com/projects/esp-idf/en/release-v4.2/esp32/api-reference/peripherals/ledc.html#ledc-high-and-low-speed-mode

// TODO Limit number of channels available
struct PcntUnit {
    PcntUnit(pcnt_unit_handle_t unit, InternalPinPtr pin)
        : unit(unit)
        , pin(pin) {
    }

    PcntUnit()
        : PcntUnit(nullptr, nullptr) {
    }

    PcntUnit(const PcntUnit& other)
        : PcntUnit(other.unit, other.pin) {
    }

    PcntUnit& operator=(const PcntUnit& other) = default;

    int getCount() const {
        int count;
        pcnt_unit_get_count(unit, &count);
        return count;
    }

    void clear() {
        pcnt_unit_clear_count(unit);
    }

    int16_t getAndClearCount() {
        int16_t count = getCount();
        if (count != 0) {
            clear();
        }
        return count;
    }

    PinPtr getPin() const {
        return pin;
    }

private:
    pcnt_unit_handle_t unit;
    InternalPinPtr pin;
};

class PcntManager {
public:
    PcntUnit registerUnit(InternalPinPtr pin) {
        pcnt_unit_config_t unitConfig = {
            .low_limit = std::numeric_limits<int16_t>::min(),
            .high_limit = std::numeric_limits<int16_t>::max(),
            .intr_priority = 0,
        };
        pcnt_unit_handle_t unit = nullptr;
        ESP_ERROR_CHECK(pcnt_new_unit(&unitConfig, &unit));

        // TODO Re-enable glitch filter
        //      The filter is disabled because enabling it locks ESP_PM_APB_FREQ_MAX,
        //      which then prevents light sleep from working.
        //
        // pcnt_glitch_filter_config_t filterConfig = {
        //     .max_glitch_ns = 1000,
        // };
        // ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(unit, &filterConfig));

        pcnt_chan_config_t channelConfig = {
            .edge_gpio_num = pin->getGpio(),
            .level_gpio_num = -1,
        };
        pcnt_channel_handle_t channel = nullptr;
        ESP_ERROR_CHECK(pcnt_new_channel(unit, &channelConfig, &channel));
        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD));

        ESP_ERROR_CHECK(pcnt_unit_enable(unit));
        ESP_ERROR_CHECK(pcnt_unit_clear_count(unit));
        ESP_ERROR_CHECK(pcnt_unit_start(unit));

        LOGD("Registered PCNT unit on pin %s",
            pin->getName().c_str());
        return PcntUnit(unit, pin);
    }
};

}    // namespace farmhub::kernel
