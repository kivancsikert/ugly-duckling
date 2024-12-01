#pragma once

#include <chrono>
#include <list>

#include <esp_event.h>
#include <esp_wifi.h>

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
        , configPortalRunning(configPortalRunning)
        , hostname(hostname)
        , powerSaveMode(powerSaveMode) {
        Log.debug("WiFi: initializing");

        // Initialize TCP/IP adapter and event loop
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());

        // Create default WiFi station interface
        esp_netif_create_default_wifi_sta();
        esp_netif_create_default_wifi_ap();

        // Initialize WiFi
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

        // Register event handlers
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFiDriver::onWiFiEvent, this));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &WiFiDriver::onIpEvent, this));

        // TODO Rewrite provisioning
        // WiFi.onEvent(
        //     [&configPortalRunning](WiFiEvent_t event, WiFiEventInfo_t info) {
        //         Log.debug("WiFi: provisioning started");
        //         configPortalRunning.set();
        //     },
        //     ARDUINO_EVENT_PROV_START);
        // WiFi.onEvent(
        //     [](WiFiEvent_t event, WiFiEventInfo_t info) {
        //         Log.debug("Received Wi-Fi credentials for SSID '%s'",
        //             (const char*) info.prov_cred_recv.ssid);
        //     },
        //     ARDUINO_EVENT_PROV_CRED_RECV);
        // WiFi.onEvent(
        //     [](WiFiEvent_t event, WiFiEventInfo_t info) {
        //         Log.debug("WiFi: provisioning failed because %s",
        //             info.prov_fail_reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR ? "authentication failed" : "AP not found");
        //     },
        //     ARDUINO_EVENT_PROV_CRED_FAIL);
        // WiFi.onEvent(
        //     [](WiFiEvent_t event, WiFiEventInfo_t info) {
        //         Log.debug("WiFi: provisioning successful");
        //     },
        //     ARDUINO_EVENT_PROV_CRED_SUCCESS);
        // WiFi.onEvent(
        //     [&configPortalRunning](WiFiEvent_t event, WiFiEventInfo_t info) {
        //         Log.debug("WiFi: provisioning finished");
        //         configPortalRunning.clear();
        //     },
        //     ARDUINO_EVENT_PROV_END);

        Task::run("wifi", 3072, [this](Task&) {
            runLoop();
        });
    }

    std::optional<String> getSsid() {
        Lock lock(metadataMutex);
        return ssid;
    }

    std::optional<String> getIp() {
        Lock lock(metadataMutex);
        return ip.transform([](const esp_ip4_addr_t& ip) {
            char ipString[16];
            esp_ip4addr_ntoa(&ip, ipString, sizeof(ipString));
            return String(ipString);
        });
    }

private:
    // Event handler for WiFi events
    static void onWiFiEvent(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
        auto* driver = static_cast<WiFiDriver*>(arg);
        switch (eventId) {
            case WIFI_EVENT_STA_CONNECTED: {
                auto event = static_cast<wifi_event_sta_connected_t*>(eventData);
                String ssid(event->ssid, event->ssid_len);
                {
                    Lock lock(driver->metadataMutex);
                    driver->ssid = ssid;
                }
                Log.debug("WiFi: Connected to the AP %s",
                    ssid.c_str());
                break;
            }
            case WIFI_EVENT_STA_DISCONNECTED: {
                auto event = static_cast<wifi_event_sta_disconnected_t*>(eventData);
                driver->networkReady.clear();
                driver->eventQueue.offer(WiFiEvent::DISCONNECTED);
                {
                    Lock lock(driver->metadataMutex);
                    driver->ssid.reset();
                }
                Log.debug("WiFi: Disconnected from the AP %s, reason: %d",
                    String(event->ssid, event->ssid_len).c_str(), event->reason);
                break;
            }
        }
    }

    // Event handler for IP events
    static void onIpEvent(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
        auto* driver = static_cast<WiFiDriver*>(arg);
        switch (eventId) {
            case IP_EVENT_STA_GOT_IP: {
                auto* event = static_cast<ip_event_got_ip_t*>(eventData);
                driver->networkReady.set();
                driver->eventQueue.offer(WiFiEvent::CONNECTED);
                {
                    Lock lock(driver->metadataMutex);
                    driver->ip = event->ip_info.ip;
                }
                Log.debug("WiFi: Got IP - " IPSTR, IP2STR(&event->ip_info.ip));
                break;
            }
            case IP_EVENT_STA_LOST_IP: {
                driver->networkReady.clear();
                driver->eventQueue.offer(WiFiEvent::DISCONNECTED);
                {
                    Lock lock(driver->metadataMutex);
                    driver->ip.reset();
                }
                Log.debug("WiFi: Lost IP");
                break;
            }
        }
    }

    inline void runLoop() {
        int clients = 0;
        bool connected = false;
        while (true) {
            for (auto event = eventQueue.pollIn(WIFI_CHECK_INTERVAL); event.has_value(); event = eventQueue.poll()) {
                switch (event.value()) {
                    case WiFiEvent::CONNECTED:
                        connected = true;
                        break;
                    case WiFiEvent::DISCONNECTED:
                        connected = false;
                        break;
                    case WiFiEvent::WANTS_CONNECT:
                        clients++;
                        break;
                    case WiFiEvent::WANTS_DISCONNECT:
                        clients--;
                        break;
                }
            }

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
#ifdef WOKWI
        Log.debug("Skipping WiFi provisioning on Wokwi");
        wifi_config_t wifi_config = {
            .sta = {
                .ssid = "Wokwi-GUEST",
                .password = "",
                .channel = 6 }
        };

        setWiFiMode(WIFI_MODE_STA, wifi_config);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());
#else
        // TODO Rewrite provisioning
        // WiFiProv.beginProvision(
        //     NETWORK_PROV_SCHEME_SOFTAP, NETWORK_PROV_SCHEME_HANDLER_NONE, NETWORK_PROV_SECURITY_1, pop, hostname.c_str(), serviceKey, nullptr, resetProvisioned);
        // WiFiProv.printQR(hostname.c_str(), pop, "softap", qr);
        // Log.debug("%s",
        //     qr.buffer.c_str());
#endif
    }

    // TODO This should probably be about setting STA only
    void setWiFiMode(wifi_mode_t mode, wifi_config_t& config) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(mode));

        if (powerSaveMode) {
            auto listenInterval = 50;
            Log.debug("WiFi enabling power save mode, listen interval: %d",
                listenInterval);
            ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
            config.sta.listen_interval = listenInterval;
        }
    }

    static constexpr const char* pop = "abcd1234";
    static constexpr const char* serviceKey = nullptr;
    static constexpr const bool resetProvisioned = false;

    void disconnect() {
        networkReady.clear();
        Log.debug("WiFi: Disconnecting");
        ESP_ERROR_CHECK(esp_wifi_disconnect());
        ESP_ERROR_CHECK(esp_wifi_stop());
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

    CopyQueue<WiFiEvent> eventQueue { "wifi-events", 16 };

    static constexpr milliseconds WIFI_QUEUE_TIMEOUT = 1s;
    static constexpr milliseconds WIFI_CHECK_INTERVAL = 5s;

    Mutex metadataMutex;
    std::optional<String> ssid;
    std::optional<esp_ip4_addr_t> ip;

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
