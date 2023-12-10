#pragma once

namespace farmhub { namespace kernel { namespace drivers {

class PwmMotorDriver {
public:
    void stop() {
        drive(true, 0);
    }

    virtual void drive(bool phase, double duty = 1) = 0;
};

}}}    // namespace farmhub::kernel::drivers
