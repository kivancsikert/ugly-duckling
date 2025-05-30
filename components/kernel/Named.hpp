#pragma once

namespace farmhub::kernel {

class Named {
protected:
    explicit Named(const std::string& name)
        : name(name) {
    }

public:
    const std::string name;
};

}    // namespace farmhub::kernel
