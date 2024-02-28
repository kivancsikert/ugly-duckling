#pragma once

#include <memory>

#include <devices/Peripheral.hpp>
#include <kernel/Configuration.hpp>
#include <kernel/drivers/MqttDriver.hpp>

#include "Ds18B20SoilSensorComponent.hpp"

using namespace farmhub::devices;
using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using std::make_unique;
using std::unique_ptr;
namespace farmhub::peripherals::environment {

class Ds18B20SoilSensorDeviceConfig
    : public ConfigurationSection {
public:
    Property<gpio_num_t> pin { this, "pin", GPIO_NUM_NC };
};

class Ds18B20SoilSensor
    : public Peripheral<EmptyConfiguration> {
public:
    Ds18B20SoilSensor(const String& name, shared_ptr<MqttDriver::MqttRoot> mqttRoot, gpio_num_t pin)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , sensor(name, mqttRoot, pin) {
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        sensor.populateTelemetry(telemetryJson);
    }

private:
    Ds18B20SoilSensorComponent sensor;
};

class Ds18B20SoilSensorFactory
    : public PeripheralFactory<Ds18B20SoilSensorDeviceConfig, EmptyConfiguration> {
public:
    Ds18B20SoilSensorFactory()
        : PeripheralFactory<Ds18B20SoilSensorDeviceConfig, EmptyConfiguration>("environment:ds18b20") {
    }

    unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const String& name, const Ds18B20SoilSensorDeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot) override {
        return make_unique<Ds18B20SoilSensor>(name, mqttRoot, deviceConfig.pin.get());
    }
};

}    // namespace farmhub::peripherals::environment
