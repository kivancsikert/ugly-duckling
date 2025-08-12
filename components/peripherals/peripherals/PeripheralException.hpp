#pragma once

#include <stdexcept>

namespace farmhub::peripherals {

class PeripheralCreationException
    : public std::runtime_error {
public:
    explicit PeripheralCreationException(const std::string& reason)
        : std::runtime_error(reason) {
    }
};

}
