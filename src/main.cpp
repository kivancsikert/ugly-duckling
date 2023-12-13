#include <atomic>
#include <chrono>

#include <Arduino.h>

#include <kernel/Application.hpp>
#include <kernel/FileSystem.hpp>
#include <kernel/Task.hpp>
#include <kernel/drivers/BatteryDriver.hpp>

#include <peripherals/Valve.hpp>

#if defined(MK4)
#include <devices/UglyDucklingMk4.hpp>
#elif defined(MK5)
#include <devices/UglyDucklingMk5.hpp>
#elif defined(MK6)
#include <devices/UglyDucklingMk6.hpp>
#else
#error "No device defined"
#endif

using namespace std::chrono;

using namespace farmhub::devices;
using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;

class Main {
public:
    Main() {
#if defined(MK4)
        device.motorDriver.wakeUp();
#elif defined(MK5)
        device.motorADriver.wakeUp();
#elif defined(MK6)
        device.motorDriver.wakeUp();
#endif
        device.begin();
    }

#if defined(MK4)
    UglyDucklingMk4 device;
#elif defined(MK5)
    UglyDucklingMk5 device;
#elif defined(MK6)
    UglyDucklingMk6 device;
#endif
};

void setup() {
    new Main();
}

void loop() {
}
