#pragma once

#include <MQTT.h>
#include <WiFi.h>

#include <kernel/Configuration.hpp>
#include <kernel/Event.hpp>
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

    MqttDriver(WiFiDriver& wifi, MdnsDriver& mdns, Config& mqttConfig, const String& instanceName)
        : IntermittentLoopTask("Keep MQTT connected", 32 * 1024)
        , wifi(wifi)
        , mdns(mdns)
        , mqttConfig(mqttConfig)
        , instanceName(instanceName)
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

protected:
    void setup() {
        if (mqttConfig.host.get().length() > 0) {
            mqttServer.hostname = mqttConfig.host.get();
            mqttServer.port = mqttConfig.port.get();
        } else {
            mdns.await();
            // TODO Handle lookup failure
            mdns.lookupService("mqtt", "tcp", &mqttServer);
        }
        // TODO Figure out the right keep alive value
        mqttClient.setKeepAlive(180);

        if (mqttServer.ip == IPAddress()) {
            mqttClient.begin(mqttServer.hostname.c_str(), mqttServer.port, wifi.getClient());
        } else {
            mqttClient.begin(mqttServer.ip.toString().c_str(), mqttServer.port, wifi.getClient());
        }

        Serial.println("MQTT: server: " + mqttServer.hostname + ":" + String(mqttServer.port)
            + ", client ID is '" + clientId + "', topic is '" + topic + "'");
    }

    int loopAndDelay() override {
        wifi.await();

        if (!mqttClient.connected()) {
            Serial.println("MQTT: Disconnected, reconnecting");

            if (!mqttClient.connect(clientId.c_str())) {
                Serial.println("MQTT: Connection failed");
                // TODO Implement exponential backoff
                return MQTT_DISCONNECTED_CHECK_INTERVAL_IN_MS;
            }

            subscribe("config", QoS::ExactlyOnce);
            subscribe("commands/#", QoS::ExactlyOnce);
            Serial.println("MQTT: Connected");
        }

        // Handle outgoing messages
        processPublishQueue();

        // Handle incoming messages
        mqttClient.loop();

        return MQTT_LOOP_INTERVAL_IN_MS;
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

    WiFiDriver& wifi;
    MdnsDriver& mdns;
    Config& mqttConfig;
    const String instanceName;
    const String clientId;
    const String topic;
    const String appConfigTopic = topic + "/config";
    const String commandTopicPrefix = topic + "/commands/";

    MdnsRecord mqttServer;
    MQTTClient mqttClient { MQTT_BUFFER_SIZE };
    QueueHandle_t publishQueue { xQueueCreate(mqttConfig.queueSize.get(), sizeof(MqttMessage*)) };

    // TODO Review these values
    static const int MQTT_LOOP_INTERVAL_IN_MS = 1000;
    static const int MQTT_DISCONNECTED_CHECK_INTERVAL_IN_MS = 1000;
    static const int MQTT_BUFFER_SIZE = 2048;
};

}}}    // namespace farmhub::kernel::drivers
