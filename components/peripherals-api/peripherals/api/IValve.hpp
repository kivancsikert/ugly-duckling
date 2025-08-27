#pragma once

namespace farmhub::peripherals::api {

class IValve {
public:
    virtual ~IValve() = default;

    virtual void setState(bool shouldBeOpen) = 0;
    virtual bool isOpen() = 0;
};

}    // namespace farmhub::peripherals::api
