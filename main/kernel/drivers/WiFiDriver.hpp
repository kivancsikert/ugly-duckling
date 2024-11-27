#pragma once

#include <chrono>
#include <list>

#include <WiFi.h>
#include <WiFiProv.h>

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
        WiFi.onEvent(
            [&configPortalRunning](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.debug("WiFi: provisioning started");
                configPortalRunning.set();
            },
            ARDUINO_EVENT_PROV_START);
        WiFi.onEvent(
            [](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.debug("Received Wi-Fi credentials for SSID '%s'",
                    (const char*) info.prov_cred_recv.ssid);
            },
            ARDUINO_EVENT_PROV_CRED_RECV);
        WiFi.onEvent(
            [](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.debug("WiFi: provisioning failed because %s",
                    info.prov_fail_reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR ? "authentication failed" : "AP not found");
            },
            ARDUINO_EVENT_PROV_CRED_FAIL);
        WiFi.onEvent(
            [](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.debug("WiFi: provisioning successful");
            },
            ARDUINO_EVENT_PROV_CRED_SUCCESS);
        WiFi.onEvent(
            [&configPortalRunning](WiFiEvent_t event, WiFiEventInfo_t info) {
                Log.debug("WiFi: provisioning finished");
                configPortalRunning.clear();
            },
            ARDUINO_EVENT_PROV_END);

        Task::run("wifi", 3072, [this](Task&) {
            runLoop();
        });
    }

private:
    inline void runLoop() {
        int clients = 0;
        while (true) {
            auto event = eventQueue.pollIn(WIFI_CHECK_INTERVAL);
            if (event.has_value()) {
                switch (event.value()) {
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
            }

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

    CopyQueue<WiFiEvent> eventQueue { "wifi-events", 16 };

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
