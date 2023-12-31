#pragma once

#include <kernel/Named.hpp>

namespace farmhub { namespace kernel {

class Component : public Named {
protected:
    Component(const String& name)
        : Named(name) {
    }
};

}}    // namespace farmhub::kernel
