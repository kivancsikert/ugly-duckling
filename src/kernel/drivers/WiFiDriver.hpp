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
    WiFiDriver(StateSource& networkReady, StateSource& configPortalRunning, const String& hostname, bool powerSaveMode)
        : networkReady(networkReady) {
        Log.debug("WiFi: initializing");

        WiFi.begin();
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
                Log.debug("WiFi: connected to %s", String(info.wifi_sta_connected.ssid, info.wifi_sta_connected.ssid_len).c_str());
            },
            ARDUINO_EVENT_WIFI_STA_CONNECTED);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.debug("WiFi: got IP %s, netmask %s, gateway %s",
                    IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str(),
                    IPAddress(info.got_ip.ip_info.netmask.addr).toString().c_str(),
                    IPAddress(info.got_ip.ip_info.gw.addr).toString().c_str());
                eventQueue.offer(WiFiEvent::CONNECTED);
            },
            ARDUINO_EVENT_WIFI_STA_GOT_IP);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.debug("WiFi: lost IP address");
                // TODO What should we do here?
                networkReady.clear();
                eventQueue.offer(WiFiEvent::DISCONNECTED);
            },
            ARDUINO_EVENT_WIFI_STA_LOST_IP);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.debug("WiFi: disconnected from %s, reason: %s",
                    String(info.wifi_sta_disconnected.ssid, info.wifi_sta_disconnected.ssid_len).c_str(),
                    WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason)));
                eventQueue.offer(WiFiEvent::DISCONNECTED);
            },
            ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

        Task::run("wifi", 3072, [this, &networkReady, hostname](Task& task) {
            WiFiState status = WiFiState::DISCONNECTED;
            int clients = 0;
            while (true) {
                eventQueue.take([this, &status, &networkReady, hostname, &task, &clients](WiFiEvent event) {
                    switch (event) {
                        case WiFiEvent::CONNECTED:
                            status = WiFiState::CONNECTED;
                            networkReady.set();
                            break;
                        case WiFiEvent::DISCONNECTED:
                            status = WiFiState::DISCONNECTED;
                            networkReady.clear();
                            break;
                        case WiFiEvent::WANTS_CONNECT:
                            Log.trace("WiFi: Wants to connect with %d existing clients",
                                clients);
                            if (clients++ == 0) {
                                Log.info("WiFi: Connecting for first client...");
                                if (!wifiManager.autoConnect(hostname.c_str())) {
                                    Log.debug("WiFi: failed to connect");
                                    // TODO Implement exponential backoff
                                    task.delay(5s);
                                }
                            }
                            break;
                        case WiFiEvent::WANTS_DISCONNECT:
                            Log.trace("WiFi: Wants to disconnect with %d existing clients",
                                clients);
                            if (--clients == 0) {
                                Log.debug("WiFi: Disconnecting because there are no more clients...");
                                WiFi.disconnect();
                                networkReady.clear();
                            }
                            break;
                    }
                });
            }
        });
    }

private:
    StateSource& acquire() {
        eventQueue.offerIn(WIFI_QUEUE_TIMEOUT, WiFiEvent::WANTS_CONNECT);
        return networkReady;
    }

    StateSource& release() {
        eventQueue.offerIn(WIFI_QUEUE_TIMEOUT, WiFiEvent::WANTS_DISCONNECT);
        return networkReady;
    }

    StateSource& networkReady;

    enum class WiFiEvent {
        CONNECTED,
        DISCONNECTED,
        WANTS_CONNECT,
        WANTS_DISCONNECT
    };

    enum class WiFiState {
        DISCONNECTED,
        CONNECTING,
        CONNECTED
    };

    WiFiManager wifiManager;
    Queue<WiFiEvent> eventQueue { "wifi-events", 16 };

    static constexpr milliseconds WIFI_QUEUE_TIMEOUT = 1s;

    friend class WiFiToken;
};

class WiFiToken {
public:
    WiFiToken(WiFiDriver& driver)
        : driver(driver) {
        driver.acquire().awaitSet();
    }

    ~WiFiToken() {
        driver.release();
    }

    // Delete copy constructor and assignment operator to prevent copying
    WiFiToken(const WiFiToken&) = delete;
    WiFiToken& operator=(const WiFiToken&) = delete;

private:
    WiFiDriver& driver;
};

}    // namespace farmhub::kernel::drivers
