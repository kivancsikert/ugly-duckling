#pragma once

#include <FileSystem.hpp>
#include <Pin.hpp>
#include <drivers/Bq27220Driver.hpp>
#include <drivers/Drv8848Driver.hpp>
#include <drivers/Ina219Driver.hpp>
#include <drivers/LedDriver.hpp>

#include <peripherals/Peripheral.hpp>
#include <peripherals/door/Door.hpp>
#include <peripherals/valve/ValveFactory.hpp>

#include <devices/DeviceDefinition.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals::door;
using namespace farmhub::peripherals::valve;

namespace farmhub::devices {

namespace pins {
DEFINE_PIN(GPIO_NUM_0, BOOT);

// Internal I2C
DEFINE_PIN(GPIO_NUM_1, SDA);
DEFINE_PIN(GPIO_NUM_2, SCL);

// Watchdog interrupt
DEFINE_PIN(GPIO_NUM_3, WDI);

// Port B pins
DEFINE_PIN(GPIO_NUM_4, IOB3, "B3");
DEFINE_PIN(GPIO_NUM_5, IOB1, "B1");
DEFINE_PIN(GPIO_NUM_6, IOB2, "B2");
DEFINE_PIN(GPIO_NUM_7, IOB4, "B4");

// Battery fuel gauge interrupt
DEFINE_PIN(GPIO_NUM_8, BAT_GAUGE);

// SPI for e-ink display
DEFINE_PIN(GPIO_NUM_9, SBUSY);
DEFINE_PIN(GPIO_NUM_10, SCS);
DEFINE_PIN(GPIO_NUM_11, SSDI);
DEFINE_PIN(GPIO_NUM_12, SSCLK);
DEFINE_PIN(GPIO_NUM_13, SRES);
DEFINE_PIN(GPIO_NUM_14, SDC);

// Port A pins
DEFINE_PIN(GPIO_NUM_15, IOA3, "A3");
DEFINE_PIN(GPIO_NUM_16, IOA1, "A1");
DEFINE_PIN(GPIO_NUM_17, IOA2, "A2");
DEFINE_PIN(GPIO_NUM_18, IOA4, "A4");

// USB
DEFINE_PIN(GPIO_NUM_19, DMINUS, "D-");
DEFINE_PIN(GPIO_NUM_20, DPLUS, "D+");

// GPIO_NUM_21 is NC

// Motor control pins
DEFINE_PIN(GPIO_NUM_35, DAIN2);
DEFINE_PIN(GPIO_NUM_36, DAIN1);
DEFINE_PIN(GPIO_NUM_37, DBIN1);
DEFINE_PIN(GPIO_NUM_38, DBIN2);

// Debug
DEFINE_PIN(GPIO_NUM_39, TCK);
DEFINE_PIN(GPIO_NUM_40, TDO);
DEFINE_PIN(GPIO_NUM_41, TDI);
DEFINE_PIN(GPIO_NUM_42, TMS);

// UART
DEFINE_PIN(GPIO_NUM_43, RXD0);
DEFINE_PIN(GPIO_NUM_44, TXD0);

// Status LEDs
DEFINE_PIN(GPIO_NUM_45, STATUS);
DEFINE_PIN(GPIO_NUM_46, STATUS2);

// Enable / disable external load
DEFINE_PIN(GPIO_NUM_47, LOADEN);

// Motor fault pin
DEFINE_PIN(GPIO_NUM_48, NFAULT);
}    // namespace pins

class Mk8Settings
    : public DeviceSettings {
public:
    Mk8Settings()
        : DeviceSettings("mk8") {
    }

    /**
     * @brief Disable the built-in current sensor for faulty revision 1 units.
     */
    Property<bool> disableIna219 { this, "disableIna219", false };
};

class UglyDucklingMk8 : public DeviceDefinition<Mk8Settings> {
public:
    explicit UglyDucklingMk8()
        : DeviceDefinition(pins::STATUS, pins::BOOT) {
        // Switch off strapping pin
        // TODO: Add a LED driver instead
        pins::STATUS2->pinMode(Pin::Mode::Output);
        pins::STATUS2->digitalWrite(1);
    }

    static std::shared_ptr<BatteryDriver> createBatteryDriver(const std::shared_ptr<I2CManager>& i2c) {
        auto batteryDriver = std::make_shared<Bq27220Driver>(
            i2c,
            pins::SDA,
            pins::SCL,
            BatteryParameters {
                .maximumVoltage = 4100,
                .bootThreshold = 3500,
                .shutdownThreshold = 3300,
            });

        Task::loop("battery-display", 4096, [batteryDriver](Task& task) {
            LOGD("Battery: %d mV, %d%%, %.1f mA",
                batteryDriver->getVoltage(),
                batteryDriver->getPercentage(),
                batteryDriver->getCurrent().value_or(0.0));
            Task::delay(1s);
        });

        return batteryDriver;
    }

protected:
    void registerDeviceSpecificPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services, const std::shared_ptr<Mk8Settings>& settings) override {
        auto motorDriver = Drv8848Driver::create(
            services.pwmManager,
            pins::DAIN1,
            pins::DAIN2,
            pins::DBIN1,
            pins::DBIN2,
            pins::NFAULT,
            pins::LOADEN);

        std::map<std::string, std::shared_ptr<PwmMotorDriver>> motors = { { "a", motorDriver->getMotorA() }, { "b", motorDriver->getMotorB() } };

        if (!settings->disableIna219.get()) {
            ina219 = std::make_shared<Ina219Driver>(
                services.i2c,
                I2CConfig {
                    .address = Ina219Driver::DEFAULT_ADDRESS,
                    .sda = pins::SDA,
                    .scl = pins::SCL,
                },
                Ina219Parameters {
                    .uRange = INA219_BUS_RANGE_16V,
                    .gain = INA219_GAIN_0_125,
                    .uResolution = INA219_RES_12BIT_1S,
                    .iResolution = INA219_RES_12BIT_1S,
                    .mode = INA219_MODE_CONT_SHUNT_BUS,
                    .shuntMilliOhm = 50,
                });

            // ina219->setEnabled(true);
            // Task::loop("power", 4096, [this](Task& task) {
            //     LOGD("INA219 readings: %f V (BUS), %f V (shunt), %f A, %f W",
            //         ina219->getBusVoltage(),
            //         ina219->getShuntVoltage(),
            //         ina219->getCurrent(),
            //         ina219->getPower());
            //     Task::delay(1s);
            // });
        }

        peripheralManager->registerFactory(valve::makeFactory(motors, ValveControlStrategyType::Latching));
        peripheralManager->registerFactory(door::makeFactory(motors));
    }

private:
    std::shared_ptr<Ina219Driver> ina219;
};

}    // namespace farmhub::devices
