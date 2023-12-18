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

        deviceDefinition.registerPeripheralFactories(peripheralManager);

        kernel.begin();

        peripheralManager.begin();

#if defined(MK4)
        deviceDefinition.motorDriver.wakeUp();
#elif defined(MK5)
        deviceDefinition.motorADriver.wakeUp();
#elif defined(MK6)
        deviceDefinition.motorDriver.wakeUp();
#endif
    }

private:
    TDeviceDefinition deviceDefinition;
    Kernel<TDeviceConfiguration> kernel { deviceDefinition.statusLed };
    PeripheralManager peripheralManager;
};

}}    // namespace farmhub::devices
