#pragma once

#include <kernel/Named.hpp>
#include <kernel/mqtt/MqttRoot.hpp>

using namespace farmhub::kernel::mqtt;

namespace farmhub::kernel {

class Component : public Named {
protected:
    Component(const std::string& name, std::shared_ptr<MqttRoot> mqttRoot)
        : Named(name)
        , mqttRoot(mqttRoot) {
    }

    std::shared_ptr<MqttRoot> mqttRoot;
};

}    // namespace farmhub::kernel
