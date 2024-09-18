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
    WiFiDriver(StateSource& networkRequested, StateSource& networkReady, StateSource& configPortalRunning, const String& hostname, bool powerSaveMode)
        : networkRequested(networkRequested)
        , networkReady(networkReady)
        , hostname(hostname)
        , powerSaveMode(powerSaveMode) {
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
                Log.debug("WiFi: connected to %s",
                    String(info.wifi_sta_connected.ssid, info.wifi_sta_connected.ssid_len).c_str());
            },
            ARDUINO_EVENT_WIFI_STA_CONNECTED);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.debug("WiFi: got IP %s, netmask %s, gateway %s",
                    IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str(),
                    IPAddress(info.got_ip.ip_info.netmask.addr).toString().c_str(),
                    IPAddress(info.got_ip.ip_info.gw.addr).toString().c_str());
                networkReady.set();
                eventQueue.offer(WiFiEvent::CONNECTED);
            },
            ARDUINO_EVENT_WIFI_STA_GOT_IP);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.debug("WiFi: lost IP address");
                networkReady.clear();
                eventQueue.offer(WiFiEvent::DISCONNECTED);
            },
            ARDUINO_EVENT_WIFI_STA_LOST_IP);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.debug("WiFi: disconnected from %s, reason: %s",
                    String(info.wifi_sta_disconnected.ssid, info.wifi_sta_disconnected.ssid_len).c_str(),
                    WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason)));
                networkReady.clear();
                eventQueue.offer(WiFiEvent::DISCONNECTED);
            },
            ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

        Task::run("wifi", 3072, [this](Task&) {
            runLoop();
        });
    }

private:
    inline void runLoop() {
        int clients = 0;
        while (true) {
            eventQueue.pollIn(WIFI_CHECK_INTERVAL, [&clients](const WiFiEvent event) {
                switch (event) {
                    case WiFiEvent::CONNECTED:
                        break;
                    case WiFiEvent::DISCONNECTED:
                        break;
                    case WiFiEvent::WANTS_CONNECT:
                        clients++;
                        break;
                    case WiFiEvent::WANTS_DISCONNECT:
                        clients--;
                        break;
                }
            });

            bool connected = WiFi.isConnected();
            if (clients > 0) {
                networkRequested.set();
                if (!connected) {
                    Log.trace("WiFi: Connecting for first client");
                    connect();
                }
            } else {
                networkRequested.clear();
                if (connected && powerSaveMode) {
                    Log.trace("WiFi: No more clients, disconnecting");
                    disconnect();
                }
            }
        }
    }

    void connect() {
        if (!wifiManager.autoConnect(hostname.c_str())) {
            Log.debug("WiFi: failed to connect, disconnecting");
            // TODO Implement exponential backoff
            disconnect();
        }
    }

    void disconnect() {
        networkReady.clear();
        WiFi.disconnect(true);
    }

    StateSource& acquire() {
        eventQueue.offerIn(WIFI_QUEUE_TIMEOUT, WiFiEvent::WANTS_CONNECT);
        return networkReady;
    }

    StateSource& release() {
        eventQueue.offerIn(WIFI_QUEUE_TIMEOUT, WiFiEvent::WANTS_DISCONNECT);
        return networkReady;
    }

    StateSource& networkRequested;
    StateSource& networkReady;
    const String hostname;
    const bool powerSaveMode;

    enum class WiFiEvent {
        CONNECTED,
        DISCONNECTED,
        WANTS_CONNECT,
        WANTS_DISCONNECT
    };

    WiFiManager wifiManager;
    Queue<WiFiEvent> eventQueue { "wifi-events", 16 };

    static constexpr milliseconds WIFI_QUEUE_TIMEOUT = 1s;
    static constexpr milliseconds WIFI_CHECK_INTERVAL = 5s;

    friend class WiFiConnection;
};

class WiFiConnection {
public:
    enum class Mode {
        Await = true,
        NoAwait = false
    };

    WiFiConnection(WiFiDriver& driver, Mode mode = Mode::Await)
        : driver(driver) {
        auto networkReady = driver.acquire();
        if (mode == Mode::NoAwait) {
            return;
        }

        try {
            networkReady.awaitSet();
        } catch (...) {
            driver.release();
            throw;
        }
    }

    void await() {
        driver.networkReady.awaitSet();
    }

    bool await(const ticks timeout) const {
        return driver.networkReady.awaitSet(timeout);
    }

    explicit operator bool() const {
        return driver.networkReady.isSet();
    }

    ~WiFiConnection() {
        driver.release();
    }

    // Delete copy constructor and assignment operator to prevent copying
    WiFiConnection(const WiFiConnection&) = delete;
    WiFiConnection& operator=(const WiFiConnection&) = delete;

private:
    WiFiDriver& driver;
};

}    // namespace farmhub::kernel::drivers
