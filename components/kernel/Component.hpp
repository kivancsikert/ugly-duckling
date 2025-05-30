#pragma once

#include <Named.hpp>
#include <mqtt/MqttRoot.hpp>
#include <utility>

using namespace farmhub::kernel::mqtt;

namespace farmhub::kernel {

class Component : public Named {
protected:
    Component(const std::string& name, std::shared_ptr<MqttRoot> mqttRoot)
        : Named(name)
        , mqttRoot(std::move(mqttRoot)) {
    }

    std::shared_ptr<MqttRoot> mqttRoot;
};

}    // namespace farmhub::kernel
