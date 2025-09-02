#pragma once

namespace farmhub::peripherals::api {

class IPeripheralManager {
public:
    virtual ~IPeripheralManager() = default;

    template <typename T>
    std::shared_ptr<T> getInstance(const std::string& name) const;
};

}
