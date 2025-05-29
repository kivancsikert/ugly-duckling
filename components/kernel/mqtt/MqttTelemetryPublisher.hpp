#pragma once

#include <memory>

#include <ArduinoJson.h>

#include <Telemetry.hpp>

namespace farmhub::kernel::mqtt {

class MqttTelemetryPublisher final
    : public TelemetryPublisher {
public:
    MqttTelemetryPublisher(std::shared_ptr<MqttRoot> mqttRoot, std::shared_ptr<TelemetryCollector> telemetryCollector)
        : mqttRoot(mqttRoot)
        , telemetryCollector(telemetryCollector) {
    }

    void publishTelemetry() {
        mqttRoot->publish("telemetry", [this](JsonObject& json) { telemetryCollector->collect(json); }, Retention::NoRetain, QoS::AtLeastOnce);
    }

private:
    const std::shared_ptr<MqttRoot> mqttRoot;
    const std::shared_ptr<TelemetryCollector> telemetryCollector;
};

}    // namespace farmhub::kernel::mqtt
