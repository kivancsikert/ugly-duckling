#pragma once

#include <Arduino.h>
#include <Wire.h>

#include <devices/Pin.hpp>
#include <kernel/Component.hpp>
#include <kernel/Configuration.hpp>

namespace farmhub::peripherals::multiplexer {

class Xl9535DeviceConfig
    : public I2CDeviceConfig {
};

class Xl9535Component
    : public Component {

public:
    Xl9535Component(
        const String& name,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        I2CManager& i2c,
        I2CConfig config)
        : Component(name, mqttRoot)
        , device(i2c.createDevice(name, config)) {
        Log.info("Initializing XL9535 multiplexer with %s",
            config.toString().c_str());
    }

    void pinMode(uint8_t pin, uint8_t mode) {
        if (mode == OUTPUT) {
            direction &= ~(1 << pin);
        } else {
            direction |= 1 << pin;
        }
        if (pin < 8) {
            updateDirection1();
        } else {
            updateDirection2();
        }
    }

    void digitalWrite(uint8_t pin, uint8_t val) {
        if (val == HIGH) {
            output |= 1 << pin;
        } else {
            output &= ~(1 << pin);
        }
        if (pin < 8) {
            updateOutput1();
        } else {
            updateOutput2();
        }
    }

    int digitalRead(uint8_t pin) {
        I2CTransmission tx(device);
        tx.write(pin < 8 ? 0x00 : 0x01);
        tx.requestFrom(1);
        uint8_t data = tx.read();
        return (data >> (pin % 8)) & 1;
    }

private:
    void updateDirection1() {
        I2CTransmission tx(device);
        tx.write(0x06);
        tx.write(direction & 0xFF);
    }

    void updateDirection2() {
        I2CTransmission tx(device);
        tx.write(0x07);
        tx.write(direction >> 8);
    }

    void updateOutput1() {
        I2CTransmission tx(device);
        tx.write(0x02);
        tx.write(output & 0xFF);
    }

    void updateOutput2() {
        I2CTransmission tx(device);
        tx.write(0x03);
        tx.write(output >> 8);
    }

    shared_ptr<I2CDevice> device;

    uint16_t polarity = 0;
    uint16_t direction = 0xFFFF;
    uint16_t output = 0;
};

class Xl9535Pin : public Pin {
public:
    Xl9535Pin(const String& name, Xl9535Component& mpx, uint8_t pin)
        : Pin(name)
        , mpx(mpx)
        , pin(pin) {
    }

    void pinMode(uint8_t mode) const override {
        mpx.pinMode(pin, mode);
    }

    void digitalWrite(uint8_t val) const override {
        mpx.digitalWrite(pin, val);
    }

    int digitalRead() const override {
        return mpx.digitalRead(pin);
    }

private:
    Xl9535Component& mpx;
    const uint8_t pin;
};

class Xl9535
    : public Peripheral<EmptyConfiguration> {
public:
    Xl9535(const String& name, shared_ptr<MqttDriver::MqttRoot> mqttRoot, I2CManager& i2c, I2CConfig config)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , component(name, mqttRoot, i2c, config) {

        // Create a pin for each bit in the pins mask
        for (int i = 0; i < 16; i++) {
            String pinName = name + ":" + String(i);
            Log.trace("Registering external pin %s",
                pinName.c_str());
            auto pin = std::make_shared<Xl9535Pin>(pinName, component, i);
            Pin::registerPin(pinName, pin);
        }
    }

private:
    Xl9535Component component;
};

class Xl9535Factory
    : public PeripheralFactory<Xl9535DeviceConfig, EmptyConfiguration> {
public:
    Xl9535Factory()
        : PeripheralFactory<Xl9535DeviceConfig, EmptyConfiguration>("multiplexer:xl9535") {
    }

    unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const String& name, const Xl9535DeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot, PeripheralServices& services) override {
        return make_unique<Xl9535>(name, mqttRoot, services.i2c, deviceConfig.parse());
    }
};

}    // namespace farmhub::peripherals::multiplexer
