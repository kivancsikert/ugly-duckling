#pragma once

#include <chrono>
#include <list>

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include <kernel/Concurrent.hpp>
#include <kernel/Log.hpp>
#include <kernel/State.hpp>
#include <kernel/Strings.hpp>
#include <kernel/Task.hpp>

using namespace std::chrono_literals;
using namespace farmhub::kernel;

namespace farmhub::kernel::drivers {

class WiFiDriver {
public:
    WiFiDriver(StateSource& networkRequested, StateSource& networkReady, StateSource& configPortalRunning, const String& hostname, bool powerSaveMode)
        : networkRequested(networkRequested)
        , networkReady(networkReady)
        , configPortalRunning(configPortalRunning)
        , hostname(hostname)
        , powerSaveMode(powerSaveMode) {
        Log.debug("WiFi: initializing");

        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_ap();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &eventHandler, this, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &eventHandler, this, NULL));

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

        Task::run("wifi", 3072, [this](Task&) {
            runLoop();
        });
    }

    static void eventHandler(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
        WiFiDriver* driver = static_cast<WiFiDriver*>(arg);
        driver->handleEvent(eventBase, eventId, eventData);
    }

private:
    void handleEvent(esp_event_base_t eventBase, int32_t eventId, void* eventData) {
        if (eventBase == WIFI_EVENT) {
            switch (eventId) {
                case WIFI_EVENT_STA_CONNECTED: {
                    wifi_event_sta_connected_t* event = static_cast<wifi_event_sta_connected_t*>(eventData);
                    Log.debug("WiFi: connected to %s",
                        String(event->ssid, event->ssid_len).c_str());
                    break;
                }
                case WIFI_EVENT_STA_DISCONNECTED: {
                    wifi_event_sta_disconnected_t* event = static_cast<wifi_event_sta_disconnected_t*>(eventData);
                    Log.debug("WiFi: disconnected from %s, reason: %u",
                        String(event->ssid, event->ssid_len).c_str(),
                        event->reason);
                    networkReady.clear();
                    eventQueue.offer(WiFiEvent::DISCONNECTED);
                    break;
                }
                case WIFI_EVENT_AP_START: {
                    Log.debug("WiFi: `softAP started");
                    configPortalRunning.set();
                    break;
                }
                case WIFI_EVENT_AP_STOP: {
                    Log.debug("WiFi: softAP finished");
                    configPortalRunning.clear();
                    break;
                }
            }
        } else if (eventBase == IP_EVENT) {
            switch (eventId) {
                case IP_EVENT_STA_GOT_IP: {
                    ip_event_got_ip_t* event = static_cast<ip_event_got_ip_t*>(eventData);
                    Log.debug("WiFi: got IP %s, netmask %s, gateway %s",
                        IPAddress(event->ip_info.ip.addr).toString().c_str(),
                        IPAddress(event->ip_info.netmask.addr).toString().c_str(),
                        IPAddress(event->ip_info.gw.addr).toString().c_str());
                    networkReady.set();
                    eventQueue.offer(WiFiEvent::CONNECTED);
                    break;
                }
                case IP_EVENT_STA_LOST_IP: {
                    Log.debug("WiFi: lost IP address");
                    networkReady.clear();
                    eventQueue.offer(WiFiEvent::DISCONNECTED);
                    break;
                }
            }
        }
    }

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
                if (!connected && !configPortalRunning.isSet()) {
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
        // Print wifi status
        Log.debug("WiFi status: %d",
            WiFi.status());
#ifdef WOKWI
        Log.debug("Skipping WiFi provisioning on Wokwi");
        // Use Wokwi WiFi network
        WiFi.begin("Wokwi-GUEST", "", 6);
#else
        StringPrint qr;
// BLE Provisioning using the ESP SoftAP Prov works fine for any BLE SoC, including ESP32, ESP32S3 and ESP32C3.
#if CONFIG_BLUEDROID_ENABLED && !defined(USE_SOFT_AP)
        Log.debug("Begin Provisioning using BLE");
        // Sample uuid that user can pass during provisioning using BLE
        uint8_t uuid[16] = { 0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf, 0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02 };
        WiFiProv.beginProvision(
            NETWORK_PROV_SCHEME_BLE, NETWORK_PROV_SCHEME_HANDLER_FREE_BLE, NETWORK_PROV_SECURITY_1, pop, hostname.c_str(), serviceKey, uuid, resetProvisioned);
        WiFiProv.printQR(hostname.c_str(), pop, "ble", qr);
#else
        Log.debug("Begin Provisioning using Soft AP");
        WiFiProv.beginProvision(
            NETWORK_PROV_SCHEME_SOFTAP, NETWORK_PROV_SCHEME_HANDLER_NONE, NETWORK_PROV_SECURITY_1, pop, hostname.c_str(), serviceKey, nullptr, resetProvisioned);
        WiFiProv.printQR(hostname.c_str(), pop, "softap", qr);
#endif
        Log.debug("%s",
            qr.buffer.c_str());
#endif
    }

    static constexpr const char* pop = "abcd1234";
    static constexpr const char* serviceKey = nullptr;
    static constexpr const bool resetProvisioned = false;

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
    StateSource& configPortalRunning;
    const String hostname;
    const bool powerSaveMode;

    enum class WiFiEvent {
        CONNECTED,
        DISCONNECTED,
        WANTS_CONNECT,
        WANTS_DISCONNECT
    };

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
