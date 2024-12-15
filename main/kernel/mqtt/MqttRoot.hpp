#pragma once

#include <chrono>

#include <kernel/mqtt/MqttDriver.hpp>

namespace farmhub::kernel::mqtt {

class MqttRoot {
public:
    MqttRoot(MqttDriver& mqtt, const String& rootTopic)
        : mqtt(mqtt)
        , rootTopic(rootTopic) {
    }

    shared_ptr<MqttRoot> forSuffix(const String& suffix) {
        return make_shared<MqttRoot>(mqtt, rootTopic + "/" + suffix);
    }

    PublishStatus publish(const String& suffix, const JsonDocument& json, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce, ticks timeout = MqttDriver::MQTT_DEFAULT_PUBLISH_TIMEOUT, LogPublish log = LogPublish::Log) {
        return mqtt.publish(fullTopic(suffix), json, retain, qos, timeout, milliseconds::zero(), log);
    }

    PublishStatus publish(const String& suffix, std::function<void(JsonObject&)> populate, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce, ticks timeout = MqttDriver::MQTT_DEFAULT_PUBLISH_TIMEOUT, LogPublish log = LogPublish::Log) {
        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        populate(root);
        return publish(suffix, doc, retain, qos, timeout, log);
    }

    PublishStatus clear(const String& suffix, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce, ticks timeout = MqttDriver::MQTT_DEFAULT_PUBLISH_TIMEOUT) {
        return mqtt.clear(fullTopic(suffix), retain, qos, timeout);
    }

    bool subscribe(const String& suffix, SubscriptionHandler handler) {
        return subscribe(suffix, QoS::ExactlyOnce, handler);
    }

    bool registerCommand(const String& name, CommandHandler handler) {
        String suffix = "commands/" + name;
        return subscribe(suffix, QoS::ExactlyOnce, [this, name, suffix, handler](const String&, const JsonObject& request) {
            // TODO Do exponential backoff when clear cannot be finished
            // Clear topic and wait for it to be cleared
            auto clearStatus = mqtt.clear(fullTopic(suffix), Retention::Retain, QoS::ExactlyOnce, std::chrono::seconds { 5 }, MQTT_ALERT_AFTER_INCOMING);
            if (clearStatus != PublishStatus::Success) {
                LOGTE(Tag::MQTT, "Failed to clear retained command topic '%s', status: %d",
                    suffix.c_str(), static_cast<int>(clearStatus));
            }

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
    bool subscribe(const String& suffix, QoS qos, SubscriptionHandler handler) {
        return mqtt.subscribe(fullTopic(suffix), qos, handler);
    }

private:
    String fullTopic(const String& suffix) const {
        return rootTopic + "/" + suffix;
    }

    MqttDriver& mqtt;
    const String rootTopic;

    static constexpr milliseconds MQTT_ALERT_AFTER_INCOMING = 30s;
};

}    // namespace farmhub::kernel::mqtt
