#pragma once

#include <MQTT.h>
#include <WiFi.h>

#include <Event.hpp>
#include <Task.hpp>
#include <drivers/MdnsDriver.hpp>
#include <drivers/WiFiDriver.hpp>

namespace farmhub { namespace device { namespace drivers {

class MqttDriver : IntermittentLoopTask {
public:
    MqttDriver(MdnsDriver& mdns, WiFiDriver& wifi)
        : IntermittentLoopTask("Keep MQTT connected")
        , mdns(mdns)
        , wifi(wifi) {
    }

protected:
    void setup() {
        Serial.println("MQTT: Waiting for mDNS to be ready");
        mdns.waitFor();
        Serial.println("MQTT: mDNS is ready");
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

}}}    // namespace farmhub::device::drivers
