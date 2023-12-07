#pragma once

#include <list>

#include <WiFi.h>

#include <WiFiManager.h>

#include <kernel/Event.hpp>
#include <kernel/Task.hpp>

#include <kernel/drivers/LedDriver.hpp>

namespace farmhub { namespace kernel { namespace drivers {

class WiFiDriver {
public:
    WiFiDriver(Event& networkReady, LedDriver& statusLed, const String& hostname) {
        Task::run("WiFi", [this, &networkReady, &statusLed, hostname](Task& task) {
            // Explicitly set mode, ESP defaults to STA+AP
            WiFi.mode(WIFI_STA);
            WiFi.setHostname(hostname.c_str());

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

            wifiManager.setConfigPortalBlocking(false);
            wifiManager.autoConnect(hostname.c_str());

            if (wifiManager.getConfigPortalActive()) {
                Serial.println("WiFi: entered config portal");
                statusLed.blinkPatternInMs({ 100, -100, 100, -100, 100, -500 });
            } else {
                Serial.println("WiFi: connected to " + WiFi.SSID());
            }
        });
    }

private:
    WiFiManager wifiManager;
};

}}}    // namespace farmhub::kernel::drivers
