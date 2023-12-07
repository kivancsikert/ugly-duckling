#pragma once

#include <list>

#include <WiFi.h>

#include <WiFiManager.h>

#include <kernel/Event.hpp>
#include <kernel/Task.hpp>

#include <kernel/drivers/LedDriver.hpp>

namespace farmhub { namespace kernel { namespace drivers {

class WiFiDriver {
public:
    WiFiDriver(Event& networkReady, LedDriver& statusLed, const String& hostname) {
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(hostname.c_str());
        wifiManager.setConfigPortalTimeout(180);
        wifiManager.setAPCallback([this, &statusLed](WiFiManager* wifiManager) {
            Serial.println("WiFi: entered config portal");
            statusLed.blinkPatternInMs({ 100, -100, 100, -100, 100, -500 });
        });
        wifiManager.setConfigPortalTimeoutCallback([this, &statusLed]() {
            Serial.println("WiFi: config portal timed out");
            statusLed.blinkPatternInMs({ 100, -100 });
        });

        WiFi.onEvent(
            [](WiFiEvent_t event, WiFiEventInfo_t info) {
                Serial.println("WiFi: connected to " + String(info.wifi_sta_connected.ssid, info.wifi_sta_connected.ssid_len));
            },
            ARDUINO_EVENT_WIFI_STA_CONNECTED);
        WiFi.onEvent(
            [&networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Serial.println("WiFi: got IP " + IPAddress(info.got_ip.ip_info.ip.addr).toString()
                    + ", netmask: " + IPAddress(info.got_ip.ip_info.netmask.addr).toString()
                    + ", gateway: " + IPAddress(info.got_ip.ip_info.gw.addr).toString());
                networkReady.emitFromISR();
            },
            ARDUINO_EVENT_WIFI_STA_GOT_IP);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Serial.println("WiFi: lost IP address");
                // TODO What should we do here?
                networkReady.clearFromISR();
                requestReconnect();
            },
            ARDUINO_EVENT_WIFI_STA_LOST_IP);
        WiFi.onEvent(
            [this, &networkReady](WiFiEvent_t event, WiFiEventInfo_t info) {
                Serial.println("WiFi: disconnected from " + String(info.wifi_sta_disconnected.ssid, info.wifi_sta_disconnected.ssid_len));
                networkReady.clearFromISR();
                requestReconnect();
            },
            ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

        Task::run("WiFi", [this, &networkReady, &statusLed, hostname](Task& task) {
            while (true) {
                bool connected = wifiManager.autoConnect(hostname.c_str());
                xSemaphoreTake(reconnectSemaphor, connected ? portMAX_DELAY : 0);
                Serial.println("WiFi: Reconnecting...");
            }
        });
    }

private:
    void requestReconnect() {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(reconnectSemaphor, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    WiFiManager wifiManager;
    QueueHandle_t reconnectSemaphor = xSemaphoreCreateBinary();
};

}}}    // namespace farmhub::kernel::drivers
