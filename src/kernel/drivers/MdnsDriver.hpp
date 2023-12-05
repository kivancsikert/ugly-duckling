#pragma once

#include <kernel/Event.hpp>

#include <ESPmDNS.h>
#include <kernel/drivers/WiFiDriver.hpp>

namespace farmhub { namespace kernel { namespace drivers {

struct MdnsRecord {
    String hostname;
    IPAddress ip;
    int port;
};

class MdnsDriver
    : Task,
      public EventSource {
public:
    MdnsDriver(
        WiFiDriver& wifi,
        const String& hostname,
        const String& instanceName,
        const String& version,
        EventGroupHandle_t eventGroup,
        int eventBit)
        : Task("mDNS")
        , EventSource(eventGroup, eventBit)
        , wifi(wifi)
        , hostname(hostname)
        , instanceName(instanceName)
        , version(version) {
    }

    bool lookupService(const String& serviceName, const String& port, MdnsRecord* record) {
        xSemaphoreTake(lookupMutex, portMAX_DELAY);
        auto result = lookupServiceUnderMutex(serviceName, port, record);
        xSemaphoreGive(lookupMutex);
        return result;
    }

protected:
    void run() override {
        wifi.await();

        MDNS.begin(hostname.c_str());
        MDNS.setInstanceName(instanceName);
        Serial.println("Advertising mDNS service " + instanceName + " on " + hostname + ".local, version: " + version);
        MDNS.addService("farmhub", "tcp", 80);
        MDNS.addServiceTxt("farmhub", "tcp", "version", version);
        Serial.println("mDNS: configured");

        emitEvent();
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

    QueueHandle_t lookupMutex { xSemaphoreCreateMutex() };

    const String hostname;
    const String instanceName;
    const String version;

    WiFiDriver& wifi;
};

}}}    // namespace farmhub::kernel::drivers
