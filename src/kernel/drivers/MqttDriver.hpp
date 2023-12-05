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
    };

    MqttDriver(WiFiDriver& wifi, MdnsDriver& mdns, Config& mqttConfig, const String& instanceName)
        : IntermittentLoopTask("Keep MQTT connected")
        , wifi(wifi)
        , mdns(mdns)
        , mqttConfig(mqttConfig)
        , instanceName(instanceName)
        , clientId(getClientId(mqttConfig.clientId.get(), instanceName))
        , topic(getTopic(mqttConfig.topic.get(), instanceName)) {
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
            Serial.println("MQTT: Connected");
        }

        // Should be connected here

        mqttClient.loop();
        return MQTT_LOOP_INTERVAL_IN_MS;
    }

private:
    WiFiDriver& wifi;
    MdnsDriver& mdns;
    Config& mqttConfig;
    const String instanceName;
    const String clientId;
    const String topic;
    const String appConfigTopic = topic + "/config";
    const String commandTopicPrefix = topic + "/commands/";

    MdnsRecord mqttServer;
    MQTTClient mqttClient;

    // TODO Review these values
    static const int MQTT_LOOP_INTERVAL_IN_MS = 1000;
    static const int MQTT_DISCONNECTED_CHECK_INTERVAL_IN_MS = 1000;
};

}}}    // namespace farmhub::kernel::drivers
