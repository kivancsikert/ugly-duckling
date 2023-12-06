#pragma once

#include <list>

#include <MQTT.h>
#include <WiFi.h>

#include <kernel/Configuration.hpp>
#include <kernel/Event.hpp>
#include <kernel/Command.hpp>
#include <kernel/Task.hpp>
#include <kernel/drivers/MdnsDriver.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

namespace farmhub { namespace kernel { namespace drivers {

class MqttDriver : IntermittentLoopTask {
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

    enum class Retention {
        NoRetain,
        Retain
    };

    enum class QoS {
        AtMostOnce = 0,
        AtLeastOnce = 1,
        ExactlyOnce = 2
    };

    typedef std::function<void(const JsonObject&, JsonObject&)> CommandHandler;

    MqttDriver(Event& networkReady, MdnsDriver& mdns, Config& mqttConfig, const String& instanceName, Configuration& appConfig)
        : IntermittentLoopTask("Keep MQTT connected", 32 * 1024)
        , networkReady(networkReady)
        , mdns(mdns)
        , mqttConfig(mqttConfig)
        , instanceName(instanceName)
        , appConfig(appConfig)
        , clientId(getClientId(mqttConfig.clientId.get(), instanceName))
        , topic(getTopic(mqttConfig.topic.get(), instanceName)) {
    }

    bool publish(const String& suffix, const JsonDocument& json, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce) {
        String fullTopic = topic + "/" + suffix;
        MqttMessage* message = new MqttMessage(fullTopic, json, retain, qos);
#ifdef DUMP_MQTT
        Serial.printf("Queuing MQTT topic '%s'%s (qos = %d): ",
            fullTopic.c_str(), (retain == Retention::Retain ? " (retain)" : ""), qos);
        serializeJsonPretty(json, Serial);
        Serial.println();
#endif
        // TODO allow some timeout here
        bool storedWithoutDropping = xQueueSend(publishQueue, &message, 0);
        if (!storedWithoutDropping) {
            Serial.println("Overflow in publish queue, dropping message");
        }
        return storedWithoutDropping;
    }

    bool publish(const String& suffix, std::function<void(JsonObject&)> populate, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce, int size = MQTT_BUFFER_SIZE) {
        DynamicJsonDocument doc(size);
        JsonObject root = doc.to<JsonObject>();
        populate(root);
        return publish(suffix, doc, retain, qos);
    }

    void registerCommand(Command& command) {
        registerCommand(command.name, [&](const JsonObject& request, JsonObject& response) {
            command.handle(request, response);
        });
    }

    void registerCommand(const String command, CommandHandler handle) {
        commandHandlers.emplace_back(command, handle);
    }

protected:
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
            + ", client ID is '" + clientId + "', topic is '" + topic + "'");
    }

    milliseconds loopAndDelay() override {
        networkReady.await();

        if (!mqttClient.connected()) {
            Serial.println("MQTT: Disconnected, reconnecting");

            if (!mqttClient.connect(clientId.c_str())) {
                Serial.println("MQTT: Connection failed");
                // TODO Implement exponential backoff
                return MQTT_DISCONNECTED_CHECK_INTERVAL;
            }

            subscribe("config", QoS::ExactlyOnce);
            subscribe("commands/#", QoS::ExactlyOnce);
            Serial.println("MQTT: Connected");
        }

        processPublishQueue();
        procesIncomingQueue();

        return MQTT_LOOP_INTERVAL;
    }

private:
    // This is kept private for now, as it is not thread-safe. Since subcscription is not currently
    // needed outside of this class, it is not a problem.
    bool subscribe(const String& suffix, QoS qos) {
        String fullTopic = topic + "/" + suffix;
        Serial.printf("Subscribing to MQTT topic '%s' with QOS = %d\n", fullTopic.c_str(), qos);
        bool success = mqttClient.subscribe(fullTopic.c_str(), static_cast<int>(qos));
        if (!success) {
            Serial.printf("Error subscribing to MQTT topic '%s', error = %d\n",
                fullTopic.c_str(), mqttClient.lastError());
        }
        return success;
    }

    void processPublishQueue() {
        // Process incoming network traffic
        mqttClient.loop();

        while (true) {
            MqttMessage* message;
            if (!xQueueReceive(publishQueue, &message, 0)) {
                break;
            }

            bool success = mqttClient.publish(message->topic, message->payload, message->length, message->retain == Retention::Retain, static_cast<int>(message->qos));
#ifdef DUMP_MQTT
            Serial.printf("Published to '%s' (size: %d)\n", message->topic, message->length);
#endif
            if (!success) {
                Serial.printf("Error publishing to MQTT topic at '%s', error = %d\n",
                    message->topic, mqttClient.lastError());
            }
            delete message;
        }
    }

    void procesIncomingQueue() {
        while (true) {
            MqttMessage* message;
            if (!xQueueReceive(incomingQueue, &message, 0)) {
                break;
            }
            String topic = message->topic;
            String payload = message->payload;

            DynamicJsonDocument json(message->length * 2);
            deserializeJson(json, payload);
            if (topic == appConfigTopic) {
                appConfig.update(json.as<JsonObject>());
            } else if (topic.startsWith(commandTopicPrefix)) {
                if (payload.isEmpty()) {
#ifdef DUMP_MQTT
                    Serial.println("Ignoring empty payload");
#endif
                    return;
                }
                auto command = topic.substring(commandTopicPrefix.length());
                Serial.printf("Received command: '%s'\n", command.c_str());
                for (auto handler : commandHandlers) {
                    if (handler.command == command) {
                        // Clear command topic
                        mqttClient.publish(topic, "", true, 0);
                        auto request = json.as<JsonObject>();
                        DynamicJsonDocument responseDoc(MQTT_BUFFER_SIZE);
                        auto response = responseDoc.to<JsonObject>();
                        handler.handle(request, response);
                        if (response.size() > 0) {
                            publish("responses/" + command, responseDoc, Retention::NoRetain, QoS::ExactlyOnce);
                        }
                        return;
                    }
                }
                Serial.printf("Unknown command: '%s'\n", command.c_str());
            } else {
                Serial.printf("Unknown topic: '%s'\n", topic.c_str());
            }

            // TODO Handle incoming messages
            delete message;
        }
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

    struct MqttMessage {
        char* topic;
        char* payload;
        size_t length;
        Retention retain;
        QoS qos;

        MqttMessage()
            : topic(nullptr)
            , payload(nullptr)
            , length(0)
            , retain(Retention::NoRetain)
            , qos(QoS::AtMostOnce) {
        }

        MqttMessage(const char* topic, const char* payload, size_t length, Retention retention, QoS qos)
            : retain(retention)
            , qos(qos)
            , topic(strdup(topic))
            , payload(strndup(payload, length))
            , length(length) {
        }

        MqttMessage(const String& topic, const JsonDocument& payload, Retention retention, QoS qos)
            : retain(retention)
            , qos(qos)
            , topic(strdup(topic.c_str())) {
            size_t length = measureJson(payload);
            // TODO Do we need to have this extra byte at the end?
            size_t bufferLength = length + 1;
            char* buffer = new char[bufferLength];
            serializeJson(payload, buffer, bufferLength);
            this->payload = buffer;
            this->length = length;
        }

        ~MqttMessage() {
            free(topic);
            free(payload);
        }
    };

    Event& networkReady;
    WiFiClient wifiClient;
    MdnsDriver& mdns;
    Config& mqttConfig;
    const String instanceName;
    Configuration& appConfig;

    const String clientId;
    const String topic;
    const String appConfigTopic = topic + "/config";
    const String commandTopicPrefix = topic + "/commands/";
    // TODO Use a map instead
    struct CommandHandlerRecord {
        CommandHandlerRecord(const String& command, CommandHandler handle)
            : command(command)
            , handle(handle) {
        }

        const String command;
        const CommandHandler handle;
    };

    std::list<CommandHandlerRecord> commandHandlers;
    MdnsRecord mqttServer;
    MQTTClient mqttClient { MQTT_BUFFER_SIZE };
    QueueHandle_t publishQueue { xQueueCreate(mqttConfig.queueSize.get(), sizeof(MqttMessage*)) };
    QueueHandle_t incomingQueue { xQueueCreate(mqttConfig.queueSize.get(), sizeof(MqttMessage*)) };

    // TODO Review these values
    static constexpr milliseconds MQTT_LOOP_INTERVAL = seconds(1);
    static constexpr milliseconds MQTT_DISCONNECTED_CHECK_INTERVAL = seconds(1);
    static const int MQTT_BUFFER_SIZE = 2048;
};

}}}    // namespace farmhub::kernel::drivers
