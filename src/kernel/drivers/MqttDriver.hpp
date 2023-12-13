#pragma once

#include <list>

#include <MQTT.h>
#include <WiFi.h>

#include <kernel/Configuration.hpp>
#include <kernel/State.hpp>
#include <kernel/Task.hpp>
#include <kernel/drivers/MdnsDriver.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

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
    struct MqttMessage {
        char* fullTopic;
        char* payload;
        size_t length;
        Retention retain;
        QoS qos;

        MqttMessage()
            : fullTopic(nullptr)
            , payload(nullptr)
            , length(0)
            , retain(Retention::NoRetain)
            , qos(QoS::AtMostOnce) {
        }

        MqttMessage(const char* fullTopic, const char* payload, size_t length, Retention retention, QoS qos)
            : retain(retention)
            , qos(qos)
            , fullTopic(strdup(fullTopic))
            , payload(strndup(payload, length))
            , length(length) {
        }

        MqttMessage(const String& fullTopic, const JsonDocument& payload, Retention retention, QoS qos)
            : retain(retention)
            , qos(qos)
            , fullTopic(strdup(fullTopic.c_str())) {
            size_t length = measureJson(payload);
            // TODO Do we need to have this extra byte at the end?
            size_t bufferLength = length + 1;
            char* buffer = new char[bufferLength];
            serializeJson(payload, buffer, bufferLength);
            this->payload = buffer;
            this->length = length;
        }

        ~MqttMessage() {
            free(fullTopic);
            free(payload);
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
        Property<unsigned int> queueSize { this, "queueSize", 16 };
    };

    MqttDriver(
        State& networkReady,
        MdnsDriver& mdns,
        Config& mqttConfig,
        const String& instanceName,
        StateSource& mqttReady)
        : networkReady(networkReady)
        , mdns(mdns)
        , mqttConfig(mqttConfig)
        , instanceName(instanceName)
        , clientId(getClientId(mqttConfig.clientId.get(), instanceName))
        , rootTopic(getTopic(mqttConfig.topic.get(), instanceName))
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
        String fullTopic = rootTopic + "/" + suffix;
        MqttMessage* message = new MqttMessage(fullTopic, json, retain, qos);
#ifdef DUMP_MQTT
        Serial.printf("Queuing MQTT topic '%s'%s (qos = %d): ",
            fullTopic.c_str(), (retain == Retention::Retain ? " (retain)" : ""), qos);
        serializeJsonPretty(json, Serial);
        Serial.println();
#endif
        return publishToQueue(message);
    }

    bool publish(const String& suffix, std::function<void(JsonObject&)> populate, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce, int size = MQTT_BUFFER_SIZE) {
        DynamicJsonDocument doc(size);
        JsonObject root = doc.to<JsonObject>();
        populate(root);
        return publish(suffix, doc, retain, qos);
    }

    bool clear(const String& suffix, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce) {
        String fullTopic = rootTopic + "/" + suffix;
        Serial.println("Clearing MQTT topic '" + fullTopic + "'");
        return publishToQueue(new MqttMessage(fullTopic.c_str(), "", 0, retain, qos));
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
        Subscription* subscription = new Subscription(suffix, qos, handler);
        bool storedWithoutDropping = xQueueSend(subscribeQueue, &subscription, 0);
        if (!storedWithoutDropping) {
            Serial.println("Overflow in subscribe queue, dropping subscription");
            delete subscription;
        }
        return storedWithoutDropping;
    }

private:
    void setup() {
        if (mqttConfig.host.get().length() > 0) {
            mqttServer.hostname = mqttConfig.host.get();
            mqttServer.port = mqttConfig.port.get();
        } else {
            // TODO Handle lookup failure
            mdns.lookupService("mqtt", "tcp", mqttServer);
        }
        // TODO Figure out the right keep alive value
        mqttClient.setKeepAlive(180);

        mqttClient.onMessageAdvanced([&](MQTTClient* client, char* topic, char* payload, int length) {
            MqttMessage* message = new MqttMessage(topic, payload, length, Retention::NoRetain, QoS::ExactlyOnce);
#ifdef DUMP_MQTT
            Serial.println("Received '" + String(topic) + "' (size: " + length + "): " + String(payload, length));
#endif
            // TODO allow some timeout here
            bool storedWithoutDropping = xQueueSend(incomingQueue, &message, 0);
            if (!storedWithoutDropping) {
                Serial.println("Overflow in incoming queue, dropping message");
            }
        });

        if (mqttServer.ip == IPAddress()) {
            mqttClient.begin(mqttServer.hostname.c_str(), mqttServer.port, wifiClient);
        } else {
            mqttClient.begin(mqttServer.ip.toString().c_str(), mqttServer.port, wifiClient);
        }

        Serial.println("MQTT: server: " + mqttServer.hostname + ":" + String(mqttServer.port)
            + ", client ID is '" + clientId + "', topic is '" + rootTopic + "'");
    }

    milliseconds loopAndDelay() {
        networkReady.awaitSet();

        if (!mqttClient.connected()) {
            Serial.println("MQTT: Disconnected, reconnecting");
            mqttReady.clear();

            if (!mqttClient.connect(clientId.c_str())) {
                Serial.println("MQTT: Connection failed");
                // TODO Implement exponential backoff
                return MQTT_DISCONNECTED_CHECK_INTERVAL;
            }

            // Re-subscribe to existing subscriptions
            for (auto& subscription : subscriptions) {
                registerSubscriptionWithMqtt(subscription.suffix, subscription.qos);
            }

            Serial.println("MQTT: Connected");
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

        while (true) {
            MqttMessage* message;
            if (!xQueueReceive(publishQueue, &message, 0)) {
                break;
            }

            bool success = mqttClient.publish(message->fullTopic, message->payload, message->length, message->retain == Retention::Retain, static_cast<int>(message->qos));
#ifdef DUMP_MQTT
            Serial.printf("Published to '%s' (size: %d)\n", message->fullTopic, message->length);
#endif
            if (!success) {
                Serial.printf("Error publishing to MQTT topic at '%s', error = %d\n",
                    message->fullTopic, mqttClient.lastError());
            }
            delete message;
        }
    }

    void processSubscriptionQueue() {
        while (true) {
            Subscription* subscription;
            if (!xQueueReceive(subscribeQueue, &subscription, 0)) {
                break;
            }

            if (registerSubscriptionWithMqtt(subscription->suffix, subscription->qos)) {
                subscriptions.push_back(*subscription);
            }
            delete subscription;
        }
    }

    void procesIncomingQueue() {
        while (true) {
            MqttMessage* message;
            if (!xQueueReceive(incomingQueue, &message, 0)) {
                break;
            }

            String fullTopic = message->fullTopic;
            String payload = message->payload;

            DynamicJsonDocument json(message->length * 2);
            deserializeJson(json, payload);
            if (payload.isEmpty()) {
#ifdef DUMP_MQTT
                Serial.println("Ignoring empty payload");
#endif
                return;
            }
            auto subTopic = fullTopic.substring(rootTopic.length() + 1);
            Serial.printf("Received message: '%s'\n", subTopic.c_str());
            for (auto subscription : subscriptions) {
                if (subscription.suffix == subTopic) {
                    auto request = json.as<JsonObject>();
                    subscription.handle(subTopic, request);
                    return;
                }
            }
            Serial.printf("Unknown subscription topic: '%s'\n", subTopic.c_str());

            // TODO Handle incoming messages
            delete message;
        }
    }

    bool publishToQueue(const MqttMessage* message) {
        // TODO allow some timeout here?
        bool storedWithoutDropping = xQueueSend(publishQueue, &message, 0);
        if (!storedWithoutDropping) {
            Serial.println("Overflow in publish queue, dropping message");
            delete message;
        }
        return storedWithoutDropping;
    }

    // Actually subscribe to the given topic
    bool registerSubscriptionWithMqtt(const String& subTopic, QoS qos) {
        String fullTopic = rootTopic + "/" + subTopic;
        Serial.printf("Subscribing to MQTT topic '%s' with QOS = %d\n", fullTopic.c_str(), qos);
        bool success = mqttClient.subscribe(fullTopic.c_str(), static_cast<int>(qos));
        if (!success) {
            Serial.printf("Error subscribing to MQTT topic '%s', error = %d\n",
                fullTopic.c_str(), mqttClient.lastError());
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
    Config& mqttConfig;
    const String instanceName;
    StateSource& mqttReady;

    const String clientId;
    const String rootTopic;

    MdnsRecord mqttServer;
    MQTTClient mqttClient { MQTT_BUFFER_SIZE };
    QueueHandle_t publishQueue { xQueueCreate(mqttConfig.queueSize.get(), sizeof(MqttMessage*)) };
    QueueHandle_t incomingQueue { xQueueCreate(mqttConfig.queueSize.get(), sizeof(MqttMessage*)) };
    QueueHandle_t subscribeQueue { xQueueCreate(mqttConfig.queueSize.get(), sizeof(Subscription*)) };
    // TODO Use a map instead
    std::list<Subscription> subscriptions;

    // TODO Review these values
    static constexpr milliseconds MQTT_LOOP_INTERVAL = seconds(1);
    static constexpr milliseconds MQTT_DISCONNECTED_CHECK_INTERVAL = seconds(1);
    static const int MQTT_BUFFER_SIZE = 2048;
};

}}}    // namespace farmhub::kernel::drivers
