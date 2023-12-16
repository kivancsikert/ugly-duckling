#pragma once

#include <functional>

namespace farmhub { namespace kernel {

template <typename T>
class ServiceRef {
public:
    ServiceRef(const String& name, T& instance)
        : name(name)
        , reference(instance) {
    }

    ServiceRef(const ServiceRef& other)
        : ServiceRef(other.name, other.reference) {
    }

    const String& getName() const {
        return name;
    }

    T& get() const {
        return reference.get();
    }

private:
    const String name;
    const std::reference_wrapper<T> reference;
};

}}    // namespace farmhub::kernel
