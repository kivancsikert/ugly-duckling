#pragma once

#include <WiFi.h>

#include <WiFiManager.h>

#include <kernel/FTask.hpp>

namespace farmhub { namespace kernel { namespace drivers {

class WiFiDriver {
public:
    WiFiDriver(Event& networkReady) {
        FTask::runTask("WiFi", [&](FTask& task) {
            // Explicitly set mode, ESP defaults to STA+AP
            WiFi.mode(WIFI_STA);

            WiFi.onEvent(
                [](WiFiEvent_t event, WiFiEventInfo_t info) {
                    Serial.println("WiFi: connected to " + String(info.wifi_sta_connected.ssid, info.wifi_sta_connected.ssid_len));
                },
                ARDUINO_EVENT_WIFI_STA_CONNECTED);
            WiFi.onEvent(
                [&](WiFiEvent_t event, WiFiEventInfo_t info) {
                    Serial.println("WiFi: got IP " + IPAddress(info.got_ip.ip_info.ip.addr).toString()
                        + ", netmask: " + IPAddress(info.got_ip.ip_info.netmask.addr).toString()
                        + ", gateway: " + IPAddress(info.got_ip.ip_info.gw.addr).toString());
                    networkReady.emitFromISR();
                },
                ARDUINO_EVENT_WIFI_STA_GOT_IP);
            WiFi.onEvent(
                [](WiFiEvent_t event, WiFiEventInfo_t info) {
                    Serial.println("WiFi: lost IP address");
                    // TODO Should we clear the event bit when disconnected?
                },
                ARDUINO_EVENT_WIFI_STA_LOST_IP);
            WiFi.onEvent(
                [](WiFiEvent_t event, WiFiEventInfo_t info) {
                    Serial.println("WiFi: disconnected from " + String(info.wifi_sta_disconnected.ssid, info.wifi_sta_disconnected.ssid_len));
                },
                ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

            wifiManager.autoConnect("AutoConnectAP");
        });
    }

private:
    WiFiManager wifiManager;
};

}}}    // namespace farmhub::kernel::drivers
