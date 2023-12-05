#pragma once

#include <Event.hpp>

#include <ESPmDNS.h>
#include <WiFi.h>

namespace farmhub { namespace device { namespace drivers {

struct MdnsRecord {
    String hostname;
    IPAddress ip;
    int port;
};

class MdnsDriver : public EventEmitter {
public:
    MdnsDriver(
        const String& hostname,
        const String& instanceName,
        const String& version,
        EventGroupHandle_t eventGroup,
        int eventBit)
        : EventEmitter(eventGroup, eventBit) {

        WiFi.onEvent(
            [this, hostname, instanceName, version](WiFiEvent_t event, WiFiEventInfo_t info) {
                MDNS.begin(hostname.c_str());
                MDNS.setInstanceName(instanceName);
                Serial.println("Advertising mDNS service " + instanceName + " on " + hostname + ".local, version: " + version);
                MDNS.addService("farmhub", "tcp", 80);
                MDNS.addServiceTxt("farmhub", "tcp", "version", version);
                Serial.println("mDNS: configured");
                emitEvent();
            },
            ARDUINO_EVENT_WIFI_STA_GOT_IP);
    }

    bool lookupService(const String& serviceName, const String& port, MdnsRecord* record) {
        xSemaphoreTake(xMutex, portMAX_DELAY);
        auto result = lookupServiceUnderMutex(serviceName, port, record);
        xSemaphoreGive(xMutex);
        return result;
    }

private:
    bool lookupServiceUnderMutex(const String& serviceName, const String& port, MdnsRecord* record) {
        auto count = MDNS.queryService(serviceName.c_str(), port.c_str());
        if (count == 0) {
            return false;
        }
        Serial.printf(" found %d services, choosing first:\n", count);
        for (int i = 0; i < count; i++) {
            Serial.printf(" %s%d) %s:%d (%s)\n",
                i == 0 ? "*" : " ",
                i + 1,
                MDNS.hostname(i).c_str(),
                MDNS.port(i),
                MDNS.IP(i).toString().c_str());
        }
        record->hostname = MDNS.hostname(0);
        record->ip = MDNS.IP(0);
        record->port = MDNS.port(0);
        return true;
    }

    QueueHandle_t xMutex { xSemaphoreCreateMutex() };
};

}}}    // namespace farmhub::device::drivers
