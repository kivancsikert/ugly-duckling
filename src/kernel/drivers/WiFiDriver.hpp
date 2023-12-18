#pragma once

#include <list>

#include <WiFi.h>

#include <ArduinoLog.h>
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
            Log.traceln("WiFi: entered config portal");
            configPortalRunning.setFromISR();
        });
        wifiManager.setConfigPortalTimeoutCallback([this, &configPortalRunning]() {
            Log.traceln("WiFi: config portal timed out");
            configPortalRunning.clearFromISR();
        });

        WiFi.onEvent(
            [](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.traceln("WiFi: connected to %s", String(info.wifi_sta_connected.ssid, info.wifi_sta_connected.ssid_len).c_str());
            },
            ARDUINO_EVENT_WIFI_STA_CONNECTED);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.traceln("WiFi: got IP %p, netmask %p, gateway %p",
                    IPAddress(info.got_ip.ip_info.ip.addr),
                    IPAddress(info.got_ip.ip_info.netmask.addr),
                    IPAddress(info.got_ip.ip_info.gw.addr));
                reconnectQueue.clear();
                networkReady.setFromISR();
            },
            ARDUINO_EVENT_WIFI_STA_GOT_IP);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.traceln("WiFi: lost IP address");
                // TODO What should we do here?
                networkReady.clearFromISR();
                reconnectQueue.overwriteFromISR(true);
            },
            ARDUINO_EVENT_WIFI_STA_LOST_IP);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.traceln("WiFi: disconnected from %s, reason: %s",
                    String(info.wifi_sta_disconnected.ssid, info.wifi_sta_disconnected.ssid_len).c_str(),
                    WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason)));
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
                Log.infoln("WiFi: Reconnecting...");
            }
        });
    }

private:
    WiFiManager wifiManager;
    Queue<bool> reconnectQueue { "wifi-reconnect", 1 };
};

}}}    // namespace farmhub::kernel::drivers
