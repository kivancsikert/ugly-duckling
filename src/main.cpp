#include <atomic>
#include <chrono>

#include <Arduino.h>

#include <kernel/Application.hpp>
#include <kernel/FileSystem.hpp>
#include <kernel/Task.hpp>
#include <kernel/drivers/BatteryDriver.hpp>

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

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

using namespace farmhub::devices;

class Main {
#if defined(MK4)
    UglyDucklingMk4 device;
#elif defined(MK5)
    UglyDucklingMk5 device;
#elif defined(MK6)
    UglyDucklingMk6 device;
#endif

public:
    void demo(const String& name, PwmMotorDriver& motor, milliseconds cycle, milliseconds switchTime = milliseconds(200)) {
        Task::loop(name.c_str(), 4096, [this, &motor, cycle, switchTime](Task& task) {
            motor.drive(MotorPhase::FORWARD, 1.0);
            task.delayUntil(switchTime);
            motor.stop();
            task.delayUntil(cycle - switchTime);
            motor.drive(MotorPhase::REVERSE, 1.0);
            task.delayUntil(switchTime);
            motor.stop();
            task.delayUntil(cycle - switchTime);
        });
    }

    Main() {
#if defined(MK4)
        device.motor.wakeUp();
        demo("motor", device.motor, seconds(10));
#elif defined(MK5)
        device.motorA.wakeUp();
        demo("motor-a", device.motorA, seconds(10));
#elif defined(MK6)
        device.motorDriver.wakeUp();
        demo("motor-a", device.motorA, seconds(10));
#endif
    }
};

void setup() {
    new Main();
}

void loop() {
}
