#pragma once

#include <chrono>
#include <list>

#include <esp_event.h>
#include <esp_wifi.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_softap.h>

#include <kernel/Concurrent.hpp>
#include <kernel/Log.hpp>
#include <kernel/State.hpp>
#include <kernel/Task.hpp>

using namespace std::chrono_literals;
using namespace farmhub::kernel;

namespace farmhub::kernel::drivers {

class WiFiDriver {
public:
    WiFiDriver(StateSource& networkRequested, StateSource& networkConnecting, StateSource& networkReady, StateSource& configPortalRunning, const String& hostname, bool powerSaveMode)
        : networkRequested(networkRequested)
        , networkConnecting(networkConnecting)
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
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFiDriver::onEvent, this));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &WiFiDriver::onEvent, this));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &WiFiDriver::onEvent, this));

        Task::run("wifi-driver", 4096, [this](Task&) {
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
    static void onEvent(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
        auto* driver = static_cast<WiFiDriver*>(arg);
        if (eventBase == WIFI_EVENT) {
            driver->onWiFiEvent(eventId, eventData);
        } else if (eventBase == IP_EVENT) {
            driver->onIpEvent(eventId, eventData);
        } else if (eventBase == WIFI_PROV_EVENT) {
            driver->onWiFiProvEvent(eventId, eventData);
        }
    }

    void onWiFiEvent(int32_t eventId, void* eventData) {
        switch (eventId) {
            case WIFI_EVENT_STA_START: {
                Log.debug("WiFi: Started");
                esp_err_t err = esp_wifi_connect();
                if (err != ESP_OK) {
                    Log.debug("WiFi: Failed to start connecting: %s", esp_err_to_name(err));
                }
                break;
            }
            case WIFI_EVENT_STA_STOP: {
                Log.debug("WiFi: Stopped");
                break;
            }
            case WIFI_EVENT_STA_CONNECTED: {
                auto event = static_cast<wifi_event_sta_connected_t*>(eventData);
                String newSsid(event->ssid, event->ssid_len);
                {
                    Lock lock(metadataMutex);
                    ssid = newSsid;
                }
                Log.debug("WiFi: Connected to the AP %s",
                    newSsid.c_str());
                break;
            }
            case WIFI_EVENT_STA_DISCONNECTED: {
                auto event = static_cast<wifi_event_sta_disconnected_t*>(eventData);
                networkReady.clear();
                networkConnecting.clear();
                {
                    Lock lock(metadataMutex);
                    ssid.reset();
                }
                eventQueue.offer(WiFiEvent::DISCONNECTED);
                Log.debug("WiFi: Disconnected from the AP %s, reason: %d",
                    String(event->ssid, event->ssid_len).c_str(), event->reason);
                break;
            }
            case WIFI_EVENT_AP_STACONNECTED: {
                Log.info("WiFi: SoftAP transport connected");
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                Log.info("WiFi: SoftAP transport disconnected");
                break;
            }
        }
    }

    void onIpEvent(int32_t eventId, void* eventData) {
        switch (eventId) {
            case IP_EVENT_STA_GOT_IP: {
                auto* event = static_cast<ip_event_got_ip_t*>(eventData);
                networkReady.set();
                networkConnecting.clear();
                {
                    Lock lock(metadataMutex);
                    ip = event->ip_info.ip;
                }
                eventQueue.offer(WiFiEvent::CONNECTED);
                Log.debug("WiFi: Got IP - " IPSTR, IP2STR(&event->ip_info.ip));
                break;
            }
            case IP_EVENT_STA_LOST_IP: {
                networkReady.clear();
                {
                    Lock lock(metadataMutex);
                    ip.reset();
                }
                eventQueue.offer(WiFiEvent::DISCONNECTED);
                Log.debug("WiFi: Lost IP");
                break;
            }
        }
    }

    void onWiFiProvEvent(int32_t eventId, void* eventData) {
        switch (eventId) {
            case WIFI_PROV_START: {
                Log.debug("WiFi: provisioning started");
                // Do not turn WiFi off until provisioning finishes
                acquire();
                break;
            }
            case WIFI_PROV_CRED_RECV: {
                auto wifiConfig = static_cast<wifi_sta_config_t*>(eventData);
                Log.debug("Received Wi-Fi credentials for SSID '%s'",
                    (const char*) wifiConfig->ssid);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                auto* reason = static_cast<wifi_prov_sta_fail_reason_t*>(eventData);
                Log.debug("WiFi: provisioning failed because %s",
                    *reason == WIFI_PROV_STA_AUTH_ERROR
                        ? "authentication failed"
                        : "AP not found");
                ESP_ERROR_CHECK(wifi_prov_mgr_reset_sm_state_on_failure());
                break;
            }
            case WIFI_PROV_CRED_SUCCESS: {
                Log.debug("WiFi: provisioning successful");
                break;
            }
            case WIFI_PROV_END: {
                Log.debug("WiFi: provisioning finished");
                wifi_prov_mgr_deinit();
                configPortalRunning.clear();
                networkConnecting.clear();
                release();
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
                if (!connected) {
                    if (networkConnecting.isSet()) {
                        Log.trace("WiFi: Already connecting");
                    } else if (configPortalRunning.isSet()) {
                        Log.trace("WiFi: Provisioning already running");
                    } else {
                        Log.trace("WiFi: Connecting for first client");
                        connect();
                    }
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
        networkConnecting.set();
#ifdef WOKWI
        Log.debug("WiFi: Skipping provisioning on Wokwi");
        wifi_config_t wifiConfig = {
            .sta = {
                .ssid = "Wokwi-GUEST",
                .password = "",
                .channel = 6,
            }
        };
        startStation(wifiConfig);
#else
        bool provisioned = false;
        ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
        if (provisioned) {
            wifi_config_t wifiConfig;
            ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifiConfig));
            Log.debug("WiFi: Connecting using stored credentials to %s (password '%s')",
                wifiConfig.sta.ssid, wifiConfig.sta.password);
            startStation(wifiConfig);
        } else {
            Log.debug("WiFi: No stored credentials, starting provisioning");
            configPortalRunning.set();
            startProvisioning();
        }
#endif
    }

    void startStation(wifi_config_t& config) {
        ESP_ERROR_CHECK(esp_wifi_stop());

        if (powerSaveMode) {
            auto listenInterval = 50;
            Log.trace("WiFi enabling power save mode, listen interval: %d",
                listenInterval);
            ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
            config.sta.listen_interval = listenInterval;
        }

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    void startProvisioning() {
        // Initialize provisioning manager
        wifi_prov_mgr_config_t config = {
            .scheme = wifi_prov_scheme_softap,
            .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
        };
        ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

        char serviceName[32];
        uint8_t mac[6];
        const char* ssid_prefix = "PROV_";
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        snprintf(serviceName, sizeof(serviceName), "%s%02X%02X%02X",
            ssid_prefix, mac[3], mac[4], mac[5]);
        Log.debug("WiFi: Starting provisioning service '%s'",
            serviceName);

        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, pop, serviceName, serviceKey));

        // TODO Maybe print QR code?
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
    StateSource& networkConnecting;
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
    static constexpr milliseconds WIFI_CHECK_INTERVAL = 1min;

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
