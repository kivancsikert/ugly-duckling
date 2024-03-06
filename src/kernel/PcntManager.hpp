#pragma once

#include <driver/pcnt.h>

#include <Arduino.h>

#include <ArduinoLog.h>

namespace farmhub::kernel {

// TODO Figure out what to do with low/high speed modes
//      See https://docs.espressif.com/projects/esp-idf/en/release-v4.2/esp32/api-reference/peripherals/ledc.html#ledc-high-and-low-speed-mode

// TODO Limit number of channels available
struct PcntUnit {
    PcntUnit(pcnt_unit_t unit, gpio_num_t pin)
        : unit(unit)
        , pin(pin) {
    }

    PcntUnit()
        : PcntUnit(PCNT_UNIT_MAX, GPIO_NUM_MAX) {
    }

    PcntUnit(const PcntUnit& other)
        : PcntUnit(other.unit, other.pin) {
    }

    int16_t getCount() {
        int16_t count;
        pcnt_get_counter_value(unit, &count);
        return count;
    }

    void clear() {
        pcnt_counter_clear(unit);
    }

    int16_t getAndClearCount() {
        int16_t count = getCount();
        if (count != 0) {
            clear();
        }
        return count;
    }

private:
    pcnt_unit_t unit;
    gpio_num_t pin;
};

class PcntManager {
public:
    PcntUnit registerUnit(gpio_num_t pin) {
        pcnt_unit_t unit = static_cast<pcnt_unit_t>(nextUnit++);

        pcnt_config_t pcntConfig;
        pcntConfig.pulse_gpio_num = pin;
        pcntConfig.ctrl_gpio_num = PCNT_PIN_NOT_USED;
        pcntConfig.lctrl_mode = PCNT_MODE_KEEP;
        pcntConfig.hctrl_mode = PCNT_MODE_KEEP;
        pcntConfig.pos_mode = PCNT_COUNT_INC;
        pcntConfig.neg_mode = PCNT_COUNT_DIS;
        pcntConfig.unit = unit;
        pcntConfig.channel = PCNT_CHANNEL_0;

        pcnt_unit_config(&pcntConfig);
        pcnt_intr_disable(unit);
        pcnt_set_filter_value(unit, 1023);
        pcnt_filter_enable(unit);
        pcnt_counter_clear(unit);

        Log.traceln("Registered PCNT unit %d on pin %d",
            unit, pin);
        return PcntUnit(unit, pin);
    }

private:
    uint8_t nextUnit = 0;
};

}    // namespace farmhub::kernel
