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
    void demoMotor(const String& name, PwmMotorDriver& motor, milliseconds cycle, milliseconds switchTime = milliseconds(200)) {
        Task::loop(name.c_str(), 4096, [&motor, cycle, switchTime](Task& task) {
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

    void demoValve(const String& name, PwmMotorDriver& motor, milliseconds cycle, milliseconds switchTime = milliseconds(200)) {
        Valve* valve = new Valve(motor, *new LatchingValveControlStrategy(switchTime));
        Task::loop(name.c_str(), 4096, [valve, cycle](Task& task) {
            valve->open();
            task.delayUntil(cycle);
            valve->close();
            task.delayUntil(cycle);
        });
    }

    Main() {
#if defined(MK4)
        device.motor.wakeUp();
        demoValve("motor", device.motor, seconds(10));
#elif defined(MK5)
        device.motorA.wakeUp();
        demoValve("motor-a", device.motorA, seconds(10));
#elif defined(MK6)
        device.motorDriver.wakeUp();
        demoValve("valve-a", device.motorA, seconds(10));
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
