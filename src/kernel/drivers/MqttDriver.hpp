#pragma once

#include <MQTT.h>
#include <WiFi.h>

#include <kernel/Event.hpp>
#include <kernel/Task.hpp>
#include <kernel/drivers/MdnsDriver.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

namespace farmhub { namespace kernel { namespace drivers {

class MqttDriver : IntermittentLoopTask {
public:
    MqttDriver(MdnsDriver& mdns, WiFiDriver& wifi)
        : IntermittentLoopTask("Keep MQTT connected")
        , mdns(mdns)
        , wifi(wifi) {
    }

protected:
    void setup() {
        // TODO Allow configuring MQTT servers manually
        mdns.await();
        // TODO Handle lookup failure
        mdns.lookupService("mqtt", "tcp", &mqttServer);
        Serial.println("MQTT: server: " + mqttServer.hostname + ":" + String(mqttServer.port) + " (" + mqttServer.ip.toString() + ")");
    }

    int loopAndDelay() override {
        if (mqttClient.connected()) {
            return MQTT_CONNECTED_CHECK_INTERVAL_IN_MS;
        }
        Serial.println("MQTT: Disconnected, reconnecting");

        if (!WiFi.isConnected()) {
            return MQTT_NO_WIFI_CHECK_INTERVAL_IN_MS;
        }

        mqttClient.begin(mqttServer.ip, mqttServer.port, wifi.getClient());
        // TODO Figure out the right keep alive value
        mqttClient.setKeepAlive(60);
        // TODO Use hostname as client ID
        mqttClient.connect("esp32");
        mqttClient.publish("test/esp32", "Hello from ESP32");
        Serial.println("MQTT: Connected");

        return MQTT_CONNECTED_CHECK_INTERVAL_IN_MS;
    }

private:
    MdnsDriver& mdns;
    MdnsRecord mqttServer;

    WiFiDriver& wifi;

    MQTTClient mqttClient;

    // TODO Review these values
    static const int MQTT_CONNECTED_CHECK_INTERVAL_IN_MS = 1000;
    static const int MQTT_NO_WIFI_CHECK_INTERVAL_IN_MS = 1000;
};

}}}
