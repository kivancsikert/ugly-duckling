#pragma once

#include <kernel/Named.hpp>
#include <kernel/drivers/MqttDriver.hpp>

using namespace farmhub::kernel::drivers;

namespace farmhub { namespace kernel {

class Component : public Named {
protected:
    Component(const String& name, shared_ptr<MqttDriver::MqttRoot> mqttRoot)
        : Named(name)
        , mqttRoot(mqttRoot) {
    }

    shared_ptr<MqttDriver::MqttRoot> mqttRoot;
};

}}    // namespace farmhub::kernel
