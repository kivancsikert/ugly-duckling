#pragma once

#include <memory>

#include <ArduinoJson.h>

#include <Telemetry.hpp>
#include <utility>

namespace farmhub::kernel::mqtt {

class MqttTelemetryPublisher final
    : public TelemetryPublisher {
public:
    MqttTelemetryPublisher(std::shared_ptr<MqttRoot> mqttRoot, std::shared_ptr<TelemetryCollector> telemetryCollector)
        : mqttRoot(std::move(mqttRoot))
        , telemetryCollector(std::move(telemetryCollector)) {
    }

    void publishTelemetry() {
        mqttRoot->publish("telemetry", [this](JsonObject& json) { telemetryCollector->collect(json); }, Retention::NoRetain, QoS::AtLeastOnce);
    }

private:
    const std::shared_ptr<MqttRoot> mqttRoot;
    const std::shared_ptr<TelemetryCollector> telemetryCollector;
};

}    // namespace farmhub::kernel::mqtt
