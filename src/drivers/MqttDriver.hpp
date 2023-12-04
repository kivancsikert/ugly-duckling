#pragma once

#include <MQTT.h>

#include <drivers/WiFiDriver.hpp>

#include <Task.hpp>

namespace farmhub { namespace device { namespace drivers {

class MqttDriver : IntermittentLoopTask {
public:
    MqttDriver(WiFiDriver& wifiDriver)
        : IntermittentLoopTask("Keep MQTT connected")
        , wifiDriver(wifiDriver) {
    }

protected:
    int loopAndDelay() override {
        if (mqttClient.connected()) {
            return MQTT_CONNECTED_CHECK_INTERVAL_IN_MS;
        }

        if (!WiFi.isConnected()) {
            return MQTT_NO_WIFI_CHECK_INTERVAL_IN_MS;
        }

        // TODO Connect via mDNS
        mqttClient.begin("bumblebee.local", 1883, wifiDriver.getClient());
        mqttClient.connect("esp32");
        mqttClient.publish("test/esp32", "Hello from ESP32");

        return MQTT_CONNECTED_CHECK_INTERVAL_IN_MS;
    }

private:
    WiFiDriver& wifiDriver;

    MQTTClient mqttClient;

    // TODO Review these values
    static const int MQTT_CONNECTED_CHECK_INTERVAL_IN_MS = 1000;
    static const int MQTT_NO_WIFI_CHECK_INTERVAL_IN_MS = 1000;
};

}}}    // namespace farmhub::device::drivers
