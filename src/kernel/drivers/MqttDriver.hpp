#pragma once

#include <list>

#include <MQTT.h>
#include <WiFi.h>

#include <kernel/Concurrent.hpp>
#include <kernel/Configuration.hpp>
#include <kernel/State.hpp>
#include <kernel/Task.hpp>
#include <kernel/drivers/MdnsDriver.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

using namespace farmhub::kernel;

namespace farmhub { namespace kernel { namespace drivers {

class MqttDriver {
public:
    enum class Retention {
        NoRetain,
        Retain
    };

    enum class QoS {
        AtMostOnce = 0,
        AtLeastOnce = 1,
        ExactlyOnce = 2
    };

    typedef std::function<void(const String&, const JsonObject&)> SubscriptionHandler;

private:
    struct Message {
        const String topic;
        const String payload;
        const Retention retain;
        const QoS qos;

        Message()
            : topic("")
            , payload("")
            , retain(Retention::NoRetain)
            , qos(QoS::AtMostOnce) {
        }

        Message(const String& topic, const String& payload, Retention retention, QoS qos)
            : topic(topic)
            , payload(payload)
            , retain(retention)
            , qos(qos) {
        }

        Message(const String& topic, const JsonDocument& payload, Retention retention, QoS qos)
            : topic(topic)
            , payload(serializeJsonToString(payload))
            , retain(retention)
            , qos(qos) {
        }

    private:
        static String serializeJsonToString(const JsonDocument& jsonPayload) {
            String payload;
            serializeJson(jsonPayload, payload);
            return payload;
        }
    };

    struct Subscription {
        const String suffix;
        const QoS qos;
        const SubscriptionHandler handle;

        Subscription(const String& suffix, QoS qos, SubscriptionHandler handle)
            : suffix(suffix)
            , qos(qos)
            , handle(handle) {
        }

        Subscription(const Subscription& other)
            : suffix(other.suffix)
            , qos(other.qos)
            , handle(other.handle) {
        }
    };

public:
    class Config : public NamedConfigurationSection {
    public:
        Config(ConfigurationSection* parent, const String& name)
            : NamedConfigurationSection(parent, name) {
        }

        Property<String> host { this, "host", "" };
        Property<unsigned int> port { this, "port", 1883 };
        Property<String> clientId { this, "clientId", "" };
        Property<String> topic { this, "topic", "" };
        Property<size_t> queueSize { this, "queueSize", 16 };
    };

    MqttDriver(
        State& networkReady,
        MdnsDriver& mdns,
        Config& config,
        const String& instanceName,
        StateSource& mqttReady)
        : networkReady(networkReady)
        , mdns(mdns)
        , config(config)
        , instanceName(instanceName)
        , clientId(getClientId(config.clientId.get(), instanceName))
        , rootTopic(getTopic(config.topic.get(), instanceName))
        , mqttReady(mqttReady) {
        Task::run("MQTT", 8192, 1, [this](Task& task) {
            setup();
            while (true) {
                auto delay = loopAndDelay();
                task.delay(delay);
            }
        });
    }

    bool publish(const String& suffix, const JsonDocument& json, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce) {
        const String topic = rootTopic + "/" + suffix;
#ifdef DUMP_MQTT
        String serializedJson;
        serializeJsonPretty(json, serializedJson);
        Log.infoln("MQTT: Queuing topic '%s'%s (qos = %d): %s",
            topic.c_str(), (retain == Retention::Retain ? " (retain)" : ""), qos, serializedJson.c_str());
#endif
        return publishQueue.offerIn(MQTT_QUEUE_TIMEOUT, topic, json, retain, qos);
    }

    bool publish(const String& suffix, std::function<void(JsonObject&)> populate, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce, int size = MQTT_BUFFER_SIZE) {
        DynamicJsonDocument doc(size);
        JsonObject root = doc.to<JsonObject>();
        populate(root);
        return publish(suffix, doc, retain, qos);
    }

    bool clear(const String& suffix, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce) {
        String topic = rootTopic + "/" + suffix;
        Log.traceln("MQTT: Clearing topic '%s'",
            topic.c_str());
        return publishQueue.offerIn(MQTT_QUEUE_TIMEOUT, topic, "", retain, qos);
    }

    bool subscribe(const String& suffix, SubscriptionHandler handler) {
        return subscribe(suffix, QoS::ExactlyOnce, handler);
    }

    /**
     * @brief Subscribes to the given topic under the topic prefix.
     *
     * Note that subscription does not support wildcards.
     */
    bool subscribe(const String& suffix, QoS qos, SubscriptionHandler handler) {
        // Allow some time for the queue to empty
        return subscribeQueue.offerIn(MQTT_QUEUE_TIMEOUT, suffix, qos, handler);
    }

private:
    void setup() {
        if (config.host.get().length() > 0) {
            mqttServer.hostname = config.host.get();
            mqttServer.port = config.port.get();
        } else {
            // TODO Handle lookup failure
            mdns.lookupService("mqtt", "tcp", mqttServer);
        }
        // TODO Figure out the right keep alive value
        mqttClient.setKeepAlive(180);

        mqttClient.onMessage([&](String& topic, String& payload) {
#ifdef DUMP_MQTT
            Log.infoln("MQTT: Received '%s' (size: %d): %s",
                topic.c_str(), payload.length(), payload.c_str());
#endif
            incomingQueue.offerIn(MQTT_QUEUE_TIMEOUT, topic, payload, Retention::NoRetain, QoS::ExactlyOnce);
        });

        if (mqttServer.ip == IPAddress()) {
            mqttClient.begin(mqttServer.hostname.c_str(), mqttServer.port, wifiClient);
        } else {
            mqttClient.begin(mqttServer.ip.toString().c_str(), mqttServer.port, wifiClient);
        }

        Log.infoln("MQTT: server: %s:%d, client ID is '%s', topic is '%s'",
            mqttServer.hostname.c_str(), mqttServer.port, clientId.c_str(), rootTopic.c_str());
    }

    ticks loopAndDelay() {
        networkReady.awaitSet();

        if (!mqttClient.connected()) {
            Log.infoln("MQTT: Disconnected, connecting");
            mqttReady.clear();

            if (!mqttClient.connect(clientId.c_str())) {
                Log.errorln("MQTT: Connection failed, error = %d",
                    mqttClient.lastError());
                // TODO Implement exponential backoff
                return MQTT_DISCONNECTED_CHECK_INTERVAL;
            }

            // Re-subscribe to existing subscriptions
            for (auto& subscription : subscriptions) {
                registerSubscriptionWithMqtt(subscription.suffix, subscription.qos);
            }

            Log.infoln("MQTT: Connected");
            mqttReady.set();
        }

        processPublishQueue();
        processSubscriptionQueue();
        procesIncomingQueue();

        return MQTT_LOOP_INTERVAL;
    }

    void processPublishQueue() {
        // Process incoming network traffic
        mqttClient.loop();

        publishQueue.drain([&](const Message& message) {
            bool success = mqttClient.publish(message.topic, message.payload, message.retain == Retention::Retain, static_cast<int>(message.qos));
#ifdef DUMP_MQTT
            Log.infoln("MQTT: Published to '%s' (size: %d)",
                message.topic.c_str(), message.payload.length());
#endif
            if (!success) {
                Log.errorln("MQTT: Error publishing to '%s', error = %d",
                    message.topic.c_str(), mqttClient.lastError());
            }
        });
    }

    void processSubscriptionQueue() {
        subscribeQueue.drain([&](const Subscription& subscription) {
            if (registerSubscriptionWithMqtt(subscription.suffix, subscription.qos)) {
                subscriptions.push_back(subscription);
            }
        });
    }

    void procesIncomingQueue() {
        incomingQueue.drain([&](const Message& message) {
            const String& topic = message.topic;
            const String& payload = message.payload;

            DynamicJsonDocument json(payload.length() * 2);
            deserializeJson(json, payload);
            if (payload.isEmpty()) {
#ifdef DUMP_MQTT
                Log.infoln("MQTT: Ignoring empty payload");
#endif
                return;
            }

            auto suffix = topic.substring(rootTopic.length() + 1);
            Log.traceln("MQTT: Received message: '%s'", suffix.c_str());
            for (auto subscription : subscriptions) {
                if (subscription.suffix == suffix) {
                    auto request = json.as<JsonObject>();
                    subscription.handle(suffix, request);
                    return;
                }
            }
            Log.warningln("MQTT: No handler for suffix '%s'",
                suffix.c_str());
        });
    }

    // Actually subscribe to the given topic
    bool registerSubscriptionWithMqtt(const String& suffix, QoS qos) {
        String topic = rootTopic + "/" + suffix;
        Log.infoln("MQTT: Subscribing to topic '%s' (qos = %d)",
            topic.c_str(), qos);
        bool success = mqttClient.subscribe(topic.c_str(), static_cast<int>(qos));
        if (!success) {
            Log.error("MQTT: Error subscribing to topic '%s', error = %d\n",
                topic.c_str(), mqttClient.lastError());
        }
        return success;
    }

    static String getClientId(const String& clientId, const String& instanceName) {
        if (clientId.length() > 0) {
            return clientId;
        }
        return "ugly-duckling-" + instanceName;
    }

    static String getTopic(const String& topic, const String& instanceName) {
        if (topic.length() > 0) {
            return topic;
        }
        return "devices/ugly-duckling/" + instanceName;
    }

    State& networkReady;
    WiFiClient wifiClient;
    MdnsDriver& mdns;
    Config& config;
    const String instanceName;

    const String clientId;
    const String rootTopic;

    StateSource& mqttReady;

    MdnsRecord mqttServer;
    MQTTClient mqttClient { MQTT_BUFFER_SIZE };
    Queue<Message> publishQueue { "mqtt-publish", config.queueSize.get() };
    Queue<Message> incomingQueue { "mqtt-incoming", config.queueSize.get() };
    Queue<Subscription> subscribeQueue { "mqtt-subscribe", config.queueSize.get() };
    // TODO Use a map instead
    std::list<Subscription> subscriptions;

    // TODO Review these values
    static constexpr milliseconds MQTT_LOOP_INTERVAL = seconds(1);
    static constexpr milliseconds MQTT_DISCONNECTED_CHECK_INTERVAL = seconds(1);
    static constexpr milliseconds MQTT_QUEUE_TIMEOUT = seconds(1);
    static const int MQTT_BUFFER_SIZE = 2048;
};
}}}    // namespace farmhub::kernel::drivers
