#pragma once

#include <functional>

#include <kernel/Named.hpp>

namespace farmhub::kernel {

template <typename T>
class ServiceRef : public Named {
public:
    ServiceRef(const std::string& name, T& instance)
        : Named(name)
        , reference(instance) {
    }

    ServiceRef(const ServiceRef& other)
        : ServiceRef(other.name, other.reference) {
    }

    const std::string& getName() const {
        return name;
    }

    T& get() const {
        return reference.get();
    }

private:
    const std::reference_wrapper<T> reference;
};

}    // namespace farmhub::kernel
