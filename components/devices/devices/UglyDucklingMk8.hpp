#pragma once

#include <FileSystem.hpp>
#include <Pin.hpp>
#include <drivers/Bq27220Driver.hpp>
#include <drivers/Drv8848Driver.hpp>
#include <drivers/LedDriver.hpp>

#include <peripherals/Peripheral.hpp>
#include <peripherals/chicken_door/ChickenDoor.hpp>
#include <peripherals/flow_control/FlowControl.hpp>
#include <peripherals/flow_meter/FlowMeter.hpp>
#include <peripherals/valve/ValvePeripheral.hpp>

#include <devices/DeviceDefinition.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals::chicken_door;
using namespace farmhub::peripherals::flow_control;
using namespace farmhub::peripherals::flow_meter;
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
        return std::make_shared<Bq27220Driver>(
            i2c,
            pins::SDA,
            pins::SCL,
            BatteryParameters {
                .maximumVoltage = 4100,
                .bootThreshold = 3500,
                .shutdownThreshold = 3300,
            });
    }

protected:
    void registerDeviceSpecificPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services, const std::shared_ptr<Mk8Settings>& /*settings*/) override {
        auto motorDriver = Drv8848Driver::create(
            services.pwmManager,
            pins::DAIN1,
            pins::DAIN2,
            pins::DBIN1,
            pins::DBIN2,
            pins::NFAULT,
            pins::LOADEN);

        std::map<std::string, std::shared_ptr<PwmMotorDriver>> motors = { { "a", motorDriver->getMotorA() }, { "b", motorDriver->getMotorB() } };

        peripheralManager->registerFactory(valve::makeFactory(motors, ValveControlStrategyType::Latching));
        peripheralManager->registerFactory(flow_meter::makeFactory());
        peripheralManager->registerFactory(flow_control::makeFactory(motors, ValveControlStrategyType::Latching));
        peripheralManager->registerFactory(chicken_door::makeFactory(motors));
    }
};

}    // namespace farmhub::devices
