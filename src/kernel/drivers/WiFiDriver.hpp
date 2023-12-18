#pragma once

#include <list>

#include <WiFi.h>

#include <WiFiManager.h>

#include <kernel/Concurrent.hpp>
#include <kernel/State.hpp>
#include <kernel/Task.hpp>

using namespace farmhub::kernel;

namespace farmhub { namespace kernel { namespace drivers {

class WiFiDriver {
public:
    WiFiDriver(StateSource& networkReady, StateSource& configPortalRunning, const String& hostname) {
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(hostname.c_str());
        wifiManager.setHostname(hostname.c_str());
        wifiManager.setConfigPortalTimeout(180);
        wifiManager.setAPCallback([this, &configPortalRunning](WiFiManager* wifiManager) {
            Serial.println("WiFi: entered config portal");
            configPortalRunning.setFromISR();
        });
        wifiManager.setConfigPortalTimeoutCallback([this, &configPortalRunning]() {
            Serial.println("WiFi: config portal timed out");
            configPortalRunning.clearFromISR();
        });

        WiFi.onEvent(
            [](WiFiEvent_t event, WiFiEventInfo_t info) {
                Serial.println("WiFi: connected to " + String(info.wifi_sta_connected.ssid, info.wifi_sta_connected.ssid_len));
            },
            ARDUINO_EVENT_WIFI_STA_CONNECTED);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Serial.println("WiFi: got IP " + IPAddress(info.got_ip.ip_info.ip.addr).toString()
                    + ", netmask: " + IPAddress(info.got_ip.ip_info.netmask.addr).toString()
                    + ", gateway: " + IPAddress(info.got_ip.ip_info.gw.addr).toString());
                reconnectQueue.clear();
                networkReady.setFromISR();
            },
            ARDUINO_EVENT_WIFI_STA_GOT_IP);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Serial.println("WiFi: lost IP address");
                // TODO What should we do here?
                networkReady.clearFromISR();
                reconnectQueue.overwriteFromISR(true);
            },
            ARDUINO_EVENT_WIFI_STA_LOST_IP);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Serial.println("WiFi: disconnected from " + String(info.wifi_sta_disconnected.ssid, info.wifi_sta_disconnected.ssid_len)
                    + ", reason: " + String(WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason))));
                networkReady.clearFromISR();
                reconnectQueue.overwriteFromISR(true);
            },
            ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

        Task::run("WiFi", 3072, [this, &networkReady, hostname](Task& task) {
            millis();
            while (true) {
                bool connected = WiFi.isConnected() || wifiManager.autoConnect(hostname.c_str());
                if (connected) {
                    reconnectQueue.take();
                } else {
                    reconnectQueue.clear();
                }
                // TODO Add exponential backoff
                task.delay(seconds(5));
                Serial.println("WiFi: Reconnecting...");
            }
        });
    }

private:
    WiFiManager wifiManager;
    Queue<bool> reconnectQueue { "wifi-reconnect", 1 };
};

}}}    // namespace farmhub::kernel::drivers
