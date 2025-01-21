#pragma once

#include <chrono>
#include <list>

#include <esp_event.h>
#include <esp_wifi.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_softap.h>

#include <kernel/BootClock.hpp>
#include <kernel/Concurrent.hpp>
#include <kernel/State.hpp>
#include <kernel/StateManager.hpp>
#include <kernel/Task.hpp>

using namespace std::chrono_literals;
using namespace farmhub::kernel;

namespace farmhub::kernel::drivers {

class WiFiDriver {
public:
    WiFiDriver(StateSource& networkConnecting, StateSource& networkReady, StateSource& configPortalRunning, const std::string& hostname, bool powerSaveMode)
        : networkConnecting(networkConnecting)
        , networkReady(networkReady)
        , configPortalRunning(configPortalRunning)
        , hostname(hostname)
        , powerSaveMode(powerSaveMode) {
        LOGTD(Tag::WIFI, "Registering WiFi handlers");

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

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));

        Task::run("wifi-driver", 4096, [this](Task&) {
            runLoop();
        });
    }

    std::optional<std::string> getSsid() {
        Lock lock(metadataMutex);
        return ssid;
    }

    std::optional<std::string> getIp() {
        Lock lock(metadataMutex);
        return ip.transform([](const esp_ip4_addr_t& ip) {
            char ipString[16];
            esp_ip4addr_ntoa(&ip, ipString, sizeof(ipString));
            return std::string(ipString);
        });
    }

    milliseconds getUptime() {
        return wifiUptimeBefore + currentWifiUptime();
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
                LOGTD(Tag::WIFI, "Started");
                stationStarted.set();
                eventQueue.offer(WiFiEvent::STARTED);
                break;
            }
            case WIFI_EVENT_STA_STOP: {
                LOGTD(Tag::WIFI, "Stopped");
                stationStarted.clear();
                break;
            }
            case WIFI_EVENT_STA_CONNECTED: {
                auto event = static_cast<wifi_event_sta_connected_t*>(eventData);
                std::string newSsid((const char*) event->ssid, event->ssid_len);
                {
                    Lock lock(metadataMutex);
                    ssid = newSsid;
                }
                LOGTD(Tag::WIFI, "Connected to the AP %s",
                    newSsid.c_str());
                break;
            }
            case WIFI_EVENT_STA_DISCONNECTED: {
                auto event = static_cast<wifi_event_sta_disconnected_t*>(eventData);
                networkReady.clear();
                {
                    Lock lock(metadataMutex);
                    ssid.reset();
                }
                eventQueue.offer(WiFiEvent::DISCONNECTED);
                LOGTD(Tag::WIFI, "Disconnected from the AP %s, reason: %d",
                    std::string((const char*) event->ssid, event->ssid_len).c_str(), event->reason);
                break;
            }
            case WIFI_EVENT_AP_STACONNECTED: {
                LOGTI(Tag::WIFI, "SoftAP transport connected");
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                LOGTI(Tag::WIFI, "SoftAP transport disconnected");
                break;
            }
        }
    }

    void onIpEvent(int32_t eventId, void* eventData) {
        switch (eventId) {
            case IP_EVENT_STA_GOT_IP: {
                auto* event = static_cast<ip_event_got_ip_t*>(eventData);
                networkReady.set();
                {
                    Lock lock(metadataMutex);
                    ip = event->ip_info.ip;
                }
                eventQueue.offer(WiFiEvent::CONNECTED);
                LOGTD(Tag::WIFI, "Got IP - " IPSTR, IP2STR(&event->ip_info.ip));
                break;
            }
            case IP_EVENT_STA_LOST_IP: {
                networkReady.clear();
                {
                    Lock lock(metadataMutex);
                    ip.reset();
                }
                eventQueue.offer(WiFiEvent::DISCONNECTED);
                LOGTD(Tag::WIFI, "Lost IP");
                break;
            }
        }
    }

    void onWiFiProvEvent(int32_t eventId, void* eventData) {
        switch (eventId) {
            case WIFI_PROV_START: {
                LOGTD(Tag::WIFI, "provisioning started");
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
                LOGTD(Tag::WIFI, "provisioning failed because %s",
                    *reason == WIFI_PROV_STA_AUTH_ERROR
                        ? "authentication failed"
                        : "AP not found");
                ESP_ERROR_CHECK(wifi_prov_mgr_reset_sm_state_on_failure());
                break;
            }
            case WIFI_PROV_CRED_SUCCESS: {
                LOGTD(Tag::WIFI, "provisioning successful");
                break;
            }
            case WIFI_PROV_END: {
                LOGTD(Tag::WIFI, "provisioning finished");
                wifi_prov_mgr_deinit();
                break;
            }
        }
    }

    void runLoop() {
        bool connected = false;
        std::optional<time_point<boot_clock>> connectingSince;
        while (true) {
            if (!connected) {
                if (configPortalRunning.isSet()) {
                    // TODO Add some sort of timeout here
                    LOGTV(Tag::WIFI, "Provisioning already running");
                    goto handleEvents;
                }
                if (networkConnecting.isSet()) {
                    if (boot_clock::now() - connectingSince.value() < WIFI_CONNECTION_TIMEOUT) {
                        LOGTV(Tag::WIFI, "Already connecting");
                        goto handleEvents;
                    }

                    LOGTI(Tag::WIFI, "Connection timed out, retrying");
                    networkConnecting.clear();
                    ensureWifiStopped();
                }
                connectingSince = boot_clock::now();
                connect();
            }

        handleEvents:
            for (auto event = eventQueue.pollIn(WIFI_CHECK_INTERVAL); event.has_value(); event = eventQueue.poll()) {
                switch (event.value()) {
                    case WiFiEvent::STARTED:
                        if (!configPortalRunning.isSet()) {
                            esp_err_t err = esp_wifi_connect();
                            if (err != ESP_OK) {
                                LOGTD(Tag::WIFI, "Failed to start connecting: %s, stopping", esp_err_to_name(err));
                                ensureWifiStopped();
                            }
                        }
                        break;
                    case WiFiEvent::CONNECTED:
                        connected = true;
                        connectingSince.reset();
                        networkConnecting.clear();
                        break;
                    case WiFiEvent::DISCONNECTED:
                        connected = false;
                        networkConnecting.clear();
                        break;
                    case WiFiEvent::PROVISIONING_FINISHED:
                        networkConnecting.clear();
                        configPortalRunning.clear();
                        break;
                }
            }
        }
    }

    void connect() {
        networkConnecting.set();

#ifdef WOKWI
        LOGTD(Tag::WIFI, "Skipping provisioning on Wokwi");
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
            LOGTD(Tag::WIFI, "Connecting using stored credentials to %s",
                wifiConfig.sta.ssid);
            connectToStation(wifiConfig);
        } else {
            LOGTD(Tag::WIFI, "No stored credentials, starting provisioning");
            configPortalRunning.set();
            startProvisioning();
        }
#endif
    }

    void disconnect() {
        if (powerSaveMode) {
            LOGTV(Tag::WIFI, "No more clients, shutting down radio to conserve power");
            ensureWifiStopped();
        } else {
            LOGTV(Tag::WIFI, "No more clients, but staying online because not saving power");
        }
    }

    milliseconds currentWifiUptime() {
        if (!wifiUpSince.has_value()) {
            return milliseconds::zero();
        }
        return duration_cast<milliseconds>(boot_clock::now() - wifiUpSince.value());
    }

    void ensureWifiStationStarted(wifi_config_t& config) {
        wifiUpSince = boot_clock::now();
        if (!stationStarted.isSet()) {
            if (powerSaveMode) {
                auto listenInterval = 50;
                LOGTV(Tag::WIFI, "Enabling power save mode, listen interval: %d",
                    listenInterval);
                ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
#ifdef SOC_PM_SUPPORT_WIFI_WAKEUP
                ESP_ERROR_CHECK(esp_sleep_enable_wifi_wakeup());
#endif
#ifdef SOC_PM_SUPPORT_BEACON_WAKEUP
                ESP_ERROR_CHECK(esp_sleep_enable_wifi_beacon_wakeup());
#endif
                config.sta.listen_interval = listenInterval;
            }

            LOGTD(Tag::WIFI, "Starting station");
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
            LOGTD(Tag::WIFI, "Stopping");
            // This might return ESP_ERR_WIFI_STOP_STATE, but we can ignore it
            esp_err_t err = esp_wifi_stop();
            if (err != ESP_OK) {
                LOGTD(Tag::WIFI, "Failed to stop WiFi: %s, assuming we are still okay", esp_err_to_name(err));
            }
        }
        auto currentUptime = currentWifiUptime();
        wifiUptimeBefore += currentUptime;
        wifiUpSince.reset();
        LOGTD(Tag::WIFI, "Stopping WiFi (was up %lld ms)", currentUptime.count());
    }

    void ensureWifiDisconnected() {
        networkReady.clear();
        if (stationStarted.isSet()) {
            LOGTD(Tag::WIFI, "Disconnecting");
            ESP_ERROR_CHECK(esp_wifi_disconnect());
        }
    }

    void connectToStation(wifi_config_t& config) {
        ensureWifiStopped();
        ensureWifiStationStarted(config);
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
        LOGTD(Tag::WIFI, "Starting provisioning service '%s'",
            serviceName);

        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, pop, serviceName, serviceKey));

        // TODO Maybe print QR code?
    }

    static constexpr const char* pop = "abcd1234";
    static constexpr const char* serviceKey = nullptr;
    static constexpr const bool resetProvisioned = false;

    StateSource& networkConnecting;
    StateSource& networkReady;
    StateSource& configPortalRunning;
    const std::string hostname;
    const bool powerSaveMode;

    StateManager internalStates;
    bool wifiInitialized = false;
    StateSource stationStarted = internalStates.createStateSource("wifi:station-started");

    enum class WiFiEvent {
        STARTED,
        CONNECTED,
        DISCONNECTED,
        PROVISIONING_FINISHED,
    };

    CopyQueue<WiFiEvent> eventQueue { "wifi-events", 16 };

    static constexpr milliseconds WIFI_QUEUE_TIMEOUT = 1s;
    static constexpr milliseconds WIFI_CONNECTION_TIMEOUT = 1min;
    static constexpr milliseconds WIFI_CHECK_INTERVAL = 1min;

    Mutex metadataMutex;
    std::optional<std::string> ssid;
    std::optional<esp_ip4_addr_t> ip;

    std::optional<time_point<boot_clock>> wifiUpSince;
    milliseconds wifiUptimeBefore = milliseconds::zero();
};

}    // namespace farmhub::kernel::drivers
