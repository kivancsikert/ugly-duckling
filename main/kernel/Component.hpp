#pragma once

#include <kernel/Named.hpp>
#include <kernel/mqtt/MqttDriver.hpp>

using namespace farmhub::kernel::mqtt;

namespace farmhub::kernel {

class Component : public Named {
protected:
    Component(const String& name, shared_ptr<MqttDriver::MqttRoot> mqttRoot)
        : Named(name)
        , mqttRoot(mqttRoot) {
    }

    shared_ptr<MqttDriver::MqttRoot> mqttRoot;
};

}    // namespace farmhub::kernel
