#pragma once

#include <chrono>

#include <kernel/mqtt/MqttDriver.hpp>

namespace farmhub::kernel::mqtt {

class MqttRoot {
public:
    MqttRoot(const std::shared_ptr<MqttDriver> mqtt, const std::string& rootTopic)
        : mqtt(mqtt)
        , rootTopic(rootTopic) {
    }

    std::shared_ptr<MqttRoot> forSuffix(const std::string& suffix) {
        return std::make_shared<MqttRoot>(mqtt, rootTopic + "/" + suffix);
    }

    PublishStatus publish(const std::string& suffix, const JsonDocument& json, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce, ticks timeout = MqttDriver::MQTT_DEFAULT_PUBLISH_TIMEOUT, LogPublish log = LogPublish::Log) {
        return mqtt->publish(fullTopic(suffix), json, retain, qos, timeout, log);
    }

    PublishStatus publish(const std::string& suffix, std::function<void(JsonObject&)> populate, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce, ticks timeout = MqttDriver::MQTT_DEFAULT_PUBLISH_TIMEOUT, LogPublish log = LogPublish::Log) {
        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        populate(root);
        return publish(suffix, doc, retain, qos, timeout, log);
    }

    PublishStatus clear(const std::string& suffix, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce, ticks timeout = MqttDriver::MQTT_DEFAULT_PUBLISH_TIMEOUT) {
        return mqtt->clear(fullTopic(suffix), retain, qos, timeout);
    }

    bool subscribe(const std::string& suffix, SubscriptionHandler handler) {
        return subscribe(suffix, QoS::ExactlyOnce, handler);
    }

    bool registerCommand(const std::string& name, CommandHandler handler) {
        std::string suffix = "commands/" + name;
        return subscribe(suffix, QoS::ExactlyOnce, [this, name, suffix, handler](const std::string&, const JsonObject& request) {
            JsonDocument responseDoc;
            auto response = responseDoc.to<JsonObject>();
            handler(request, response);
            if (response.size() > 0) {
                publish("responses/" + name, responseDoc, Retention::NoRetain, QoS::ExactlyOnce);
            }
        });
    }

    void registerCommand(Command& command) {
        registerCommand(command.name, [&](const JsonObject& request, JsonObject& response) {
            command.handle(request, response);
        });
    }

    /**
     * @brief Subscribes to the given topic under the topic prefix.
     *
     * Note that subscription does not support wildcards.
     */
    bool subscribe(const std::string& suffix, QoS qos, SubscriptionHandler handler) {
        return mqtt->subscribe(fullTopic(suffix), qos, handler);
    }

private:
    std::string fullTopic(const std::string& suffix) const {
        return rootTopic + "/" + suffix;
    }

    const std::shared_ptr<MqttDriver> mqtt;
    const std::string rootTopic;
};

}    // namespace farmhub::kernel::mqtt
