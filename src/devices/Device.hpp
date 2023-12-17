#pragma once

#include <chrono>

#include <kernel/Kernel.hpp>
#include <peripherals/Valve.hpp>

using namespace std::chrono;
using namespace farmhub::kernel;
using namespace farmhub::peripherals;

#if defined(MK4)
#include <devices/UglyDucklingMk4.hpp>
typedef farmhub::devices::UglyDucklingMk4 TDeviceDefinition;
typedef farmhub::devices::Mk4Config TDeviceConfiguration;

#elif defined(MK5)
#include <devices/UglyDucklingMk5.hpp>
typedef farmhub::devices::UglyDucklingMk5 TDeviceDefinition;
typedef farmhub::devices::Mk5Config TDeviceConfiguration;
#define HAS_BATTERY

#elif defined(MK6)
#include <devices/UglyDucklingMk6.hpp>
typedef farmhub::devices::UglyDucklingMk6 TDeviceDefinition;
typedef farmhub::devices::Mk6Config TDeviceConfiguration;
#define HAS_BATTERY

#else
#error "No device defined"
#endif

namespace farmhub { namespace devices {

class ConsoleProvider {
public:
    ConsoleProvider() {
        Serial.begin(115200);
        Serial.println("Starting up...");
    }
};

class Device : ConsoleProvider {
public:
    Device() {

#ifdef HAS_BATTERY
#ifdef FARMHUB_DEBUG
        kernel.consolePrinter.registerBattery(deviceDefinition.batteryDriver);
#endif
        kernel.registerTelemetryProvider("battery", deviceDefinition.batteryDriver);
#endif
        kernel.begin();

#if defined(MK4)
        deviceDefinition.motorDriver.wakeUp();
        demoValve("motor", deviceDefinition.motor, seconds(10));
#elif defined(MK5)
        deviceDefinition.motorADriver.wakeUp();
        demoValve("motor-a", deviceDefinition.motorA, seconds(10));
#elif defined(MK6)
        deviceDefinition.motorDriver.wakeUp();
        demoValve("valve-a", deviceDefinition.motorA, seconds(10));
#endif
    }

private:
    void demoValve(const String& name, const ServiceRef<PwmMotorDriver>& motor, milliseconds cycle, milliseconds switchTime = milliseconds(200)) {
        Valve* valve = new Valve(motor.get(), *new LatchingValveControlStrategy(switchTime));
        Task::loop(name.c_str(), 4096, [valve, cycle](Task& task) {
            valve->open();
            task.delayUntil(cycle);
            valve->close();
            task.delayUntil(cycle);
        });
    }

    TDeviceDefinition deviceDefinition;
    Kernel<TDeviceConfiguration> kernel { deviceDefinition.statusLed };
};

}}    // namespace farmhub::devices
