#pragma once

#include <chrono>
#include <list>

#include <esp_event.h>
#include <esp_wifi.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_softap.h>

#include <kernel/Concurrent.hpp>
#include <kernel/State.hpp>
#include <kernel/StateManager.hpp>
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
        LOGTD("wifi", "Registering WiFi handlers");

        // Initialize TCP/IP adapter and event loop
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());

        // Create default WiFi station interface
        esp_netif_create_default_wifi_sta();
        esp_netif_create_default_wifi_ap();

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
                LOGTD("wifi", "Started");
                stationStarted.set();
                if (networkRequested.isSet()) {
                    esp_err_t err = esp_wifi_connect();
                    if (err != ESP_OK) {
                        LOGTD("wifi", "Failed to start connecting: %s", esp_err_to_name(err));
                    }
                }
                break;
            }
            case WIFI_EVENT_STA_STOP: {
                LOGTD("wifi", "Stopped");
                stationStarted.clear();
                break;
            }
            case WIFI_EVENT_STA_CONNECTED: {
                auto event = static_cast<wifi_event_sta_connected_t*>(eventData);
                String newSsid(event->ssid, event->ssid_len);
                {
                    Lock lock(metadataMutex);
                    ssid = newSsid;
                }
                LOGTD("wifi", "Connected to the AP %s",
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
                LOGTD("wifi", "Disconnected from the AP %s, reason: %d",
                    String(event->ssid, event->ssid_len).c_str(), event->reason);
                break;
            }
            case WIFI_EVENT_AP_STACONNECTED: {
                LOGTI("wifi", "SoftAP transport connected");
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                LOGTI("wifi", "SoftAP transport disconnected");
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
                LOGTD("wifi", "Got IP - " IPSTR, IP2STR(&event->ip_info.ip));
                break;
            }
            case IP_EVENT_STA_LOST_IP: {
                networkReady.clear();
                {
                    Lock lock(metadataMutex);
                    ip.reset();
                }
                eventQueue.offer(WiFiEvent::DISCONNECTED);
                LOGTD("wifi", "Lost IP");
                break;
            }
        }
    }

    void onWiFiProvEvent(int32_t eventId, void* eventData) {
        switch (eventId) {
            case WIFI_PROV_START: {
                LOGTD("wifi", "provisioning started");
                // Do not turn WiFi off until provisioning finishes
                acquire();
                break;
            }
            case WIFI_PROV_CRED_RECV: {
                auto wifiConfig = static_cast<wifi_sta_config_t*>(eventData);
                LOGD("Received Wi-Fi credentials for SSID '%s'",
                    (const char*) wifiConfig->ssid);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                auto* reason = static_cast<wifi_prov_sta_fail_reason_t*>(eventData);
                LOGTD("wifi", "provisioning failed because %s",
                    *reason == WIFI_PROV_STA_AUTH_ERROR
                        ? "authentication failed"
                        : "AP not found");
                ESP_ERROR_CHECK(wifi_prov_mgr_reset_sm_state_on_failure());
                break;
            }
            case WIFI_PROV_CRED_SUCCESS: {
                LOGTD("wifi", "provisioning successful");
                break;
            }
            case WIFI_PROV_END: {
                LOGTD("wifi", "provisioning finished");
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
                        LOGTV("wifi", "Already connecting");
                    } else if (configPortalRunning.isSet()) {
                        LOGTV("wifi", "Provisioning already running");
                    } else {
                        LOGTV("wifi", "Connecting for first client");
                        connect();
                    }
                }
            } else {
                networkRequested.clear();
                if (connected) {
                    disconnect();
                }
            }
        }
    }

    void connect() {
        networkConnecting.set();
        ensureWifiInitialized();

#ifdef WOKWI
        LOGTD("wifi", "Skipping provisioning on Wokwi");
        wifi_config_t wifiConfig = {
            .sta = {
                .ssid = "Wokwi-GUEST",
                .password = "",
                .channel = 6,
            }
        };
        connectToStation(wifiConfig);
#else
        bool provisioned = false;
        ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
        if (provisioned) {
            wifi_config_t wifiConfig;
            ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifiConfig));
            LOGTD("wifi", "Connecting using stored credentials to %s (password '%s')",
                wifiConfig.sta.ssid, wifiConfig.sta.password);
            connectToStation(wifiConfig);
        } else {
            LOGTD("wifi", "No stored credentials, starting provisioning");
            configPortalRunning.set();
            startProvisioning();
        }
#endif
    }

    void disconnect() {
        if (powerSaveMode) {
            LOGTV("wifi", "No more clients, shutting down radio to conserve power");
            ensureWifiDeinitialized();
        } else {
            LOGTV("wifi", "No more clients, but staying online because not saving power");
        }
    }

    void ensureWifiInitialized() {
        if (!wifiInitialized) {
            // Initialize WiFi
            LOGTD("wifi", "Initializing");
            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            ESP_ERROR_CHECK(esp_wifi_init(&cfg));
            ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
            wifiInitialized = true;
        }
    }

    void ensureWifiDeinitialized() {
        if (wifiInitialized) {
            ensureWifiStopped();
            LOGTD("wifi", "De-initializing");
            ESP_ERROR_CHECK(esp_wifi_deinit());
            wifiInitialized = false;
        }
    }

    void ensureWifiStationStarted(wifi_config_t& config) {
        ensureWifiInitialized();
        if (!stationStarted.isSet()) {
            if (powerSaveMode) {
                auto listenInterval = 50;
                LOGV("WiFi enabling power save mode, listen interval: %d",
                    listenInterval);
                ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
                config.sta.listen_interval = listenInterval;
            }

            LOGTD("wifi", "Starting station");
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
            ESP_ERROR_CHECK(esp_wifi_start());
            stationStarted.awaitSet();
        }
    }

    void ensureWifiStopped() {
        if (stationStarted.isSet()) {
            if (networkReady.isSet()) {
                ensureWifiDisconnected();
            }
            LOGTD("wifi", "Stopping");
            ESP_ERROR_CHECK(esp_wifi_stop());
        }
    }

    void ensureWifiDisconnected() {
        networkReady.clear();
        if (stationStarted.isSet()) {
            LOGTD("wifi", "Disconnecting");
            ESP_ERROR_CHECK(esp_wifi_disconnect());
        }
    }

    void connectToStation(wifi_config_t& config) {
        ensureWifiStopped();
        ensureWifiStationStarted(config);
    }

    void startProvisioning() {
        ensureWifiInitialized();

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
        LOGTD("wifi", "Starting provisioning service '%s'",
            serviceName);

        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, pop, serviceName, serviceKey));

        // TODO Maybe print QR code?
    }

    static constexpr const char* pop = "abcd1234";
    static constexpr const char* serviceKey = nullptr;
    static constexpr const bool resetProvisioned = false;

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

    StateManager internalStates;
    bool wifiInitialized = false;
    StateSource stationStarted = internalStates.createStateSource("wifi:station-started");

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
