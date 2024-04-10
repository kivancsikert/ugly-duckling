#pragma once

#include <chrono>
#include <list>

#include <WiFi.h>

#include <esp_wifi.h>

#include <WiFiManager.h>

#include <kernel/Concurrent.hpp>
#include <kernel/Log.hpp>
#include <kernel/State.hpp>
#include <kernel/Task.hpp>

using namespace std::chrono_literals;
using namespace farmhub::kernel;

namespace farmhub::kernel::drivers {

class WiFiDriver {
public:
    WiFiDriver(StateSource& networkReady, StateSource& configPortalRunning, const String& hostname, bool powerSaveMode) {
        Log.info("WiFi: initializing");

        wifiManager.setHostname(hostname.c_str());
        wifiManager.setConfigPortalTimeout(180);
        wifiManager.setAPCallback([this, &configPortalRunning](WiFiManager* wifiManager) {
            Log.debug("WiFi: entered config portal");
            configPortalRunning.setFromISR();
        });
        wifiManager.setConfigPortalTimeoutCallback([this, &configPortalRunning]() {
            Log.debug("WiFi: config portal timed out");
            configPortalRunning.clearFromISR();
        });

        WiFi.onEvent(
            [](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.info("WiFi: connected to %s", String(info.wifi_sta_connected.ssid, info.wifi_sta_connected.ssid_len).c_str());
            },
            ARDUINO_EVENT_WIFI_STA_CONNECTED);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.info("WiFi: got IP %p, netmask %p, gateway %p",
                    IPAddress(info.got_ip.ip_info.ip.addr),
                    IPAddress(info.got_ip.ip_info.netmask.addr),
                    IPAddress(info.got_ip.ip_info.gw.addr));
                reconnectQueue.clear();
                networkReady.set();
            },
            ARDUINO_EVENT_WIFI_STA_GOT_IP);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.info("WiFi: lost IP address");
                // TODO What should we do here?
                networkReady.clear();
                reconnectQueue.overwrite(true);
            },
            ARDUINO_EVENT_WIFI_STA_LOST_IP);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.info("WiFi: disconnected from %s, reason: %s",
                    String(info.wifi_sta_disconnected.ssid, info.wifi_sta_disconnected.ssid_len).c_str(),
                    WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason)));
                networkReady.clear();
                reconnectQueue.overwrite(true);
            },
            ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

        Task::run("wifi", 3072, [this, &networkReady, hostname, powerSaveMode](Task& task) {
            while (true) {
                bool connected = WiFi.isConnected() || wifiManager.autoConnect(hostname.c_str());
                if (connected) {
                    if (powerSaveMode) {
                        auto listenInterval = 50;
                        Log.debug("WiFi enabling power save mode, listen interval: %d",
                            listenInterval);
                        esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
                        wifi_config_t conf;
                        ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &conf));
                        conf.sta.listen_interval = listenInterval;
                        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &conf));
                    }
                    reconnectQueue.take();
                } else {
                    reconnectQueue.clear();
                }
                // TODO Add exponential backoff
                task.delay(5s);
                Log.info("WiFi: Reconnecting...");
            }
        });
    }

private:
    WiFiManager wifiManager;
    Queue<bool> reconnectQueue { "wifi-reconnect", 1 };
};

}    // namespace farmhub::kernel::drivers
