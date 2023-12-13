#pragma once

namespace farmhub { namespace kernel {

template <typename T>
class Service {
public:
    Service(const String& name, T& instance)
        : name(name)
        , instance(instance) {
    }

    Service(Service& other)
        : Service(other.name, other.instance) {
    }

    const String& getName() const {
        return name;
    }

    T& get() const {
        return instance;
    }

private:
    const String name;
    T& instance;
};
}}    // namespace farmhub::kernel
