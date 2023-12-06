#pragma once

#include <ArduinoJson.h>
#include <ESPmDNS.h>

#include <kernel/Event.hpp>
#include <kernel/NvmStore.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

namespace farmhub { namespace kernel { namespace drivers {

struct MdnsRecord {
    String hostname;
    IPAddress ip;
    int port;

    bool validate() {
        return hostname.length() > 0 && ip != IPAddress() && port > 0;
    }
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
        // TODO Add error handling
        MDNS.begin(hostname);
        MDNS.setInstanceName(instanceName);
        Serial.println("mDNS: initialized");
    }

    bool lookupService(const String& serviceName, const String& port, MdnsRecord& record) {
        xSemaphoreTake(lookupMutex, portMAX_DELAY);
        auto result = lookupServiceUnderMutex(serviceName, port, record);
        xSemaphoreGive(lookupMutex);
        return result;
    }

protected:
    void run() override {
        wifi.await();

        Serial.println("Advertising mDNS service " + instanceName + " on " + hostname + ".local, version: " + version);
        MDNS.addService("farmhub", "tcp", 80);
        MDNS.addServiceTxt("farmhub", "tcp", "version", version);
        Serial.println("mDNS: configured");

        emitEvent();
    }

private:
    bool lookupServiceUnderMutex(const String& serviceName, const String& port, MdnsRecord& record) {
        // TODO Use a callback and retry if cached entry doesn't work
        String cacheKey = serviceName + "." + port;
        if (nvm.get(cacheKey, record)) {
            if (record.validate()) {
                Serial.println("mDNS: found " + cacheKey + " in NVM cache: " + record.hostname);
                return true;
            } else {
                Serial.println("mDNS: invalid record in NVM cache for " + cacheKey + ", removing");
                nvm.remove(cacheKey);
            }
        }

        wifi.await();
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
        record.hostname = MDNS.hostname(0);
        record.ip = MDNS.IP(0);
        record.port = MDNS.port(0);

        nvm.set(cacheKey, record);

        return true;
    }

    QueueHandle_t lookupMutex { xSemaphoreCreateMutex() };

    const String hostname;
    const String instanceName;
    const String version;

    WiFiDriver& wifi;
    NvmStore nvm { "mdns" };
};

bool convertToJson(const MdnsRecord& src, JsonVariant dst) {
    auto jsonRecord = dst.to<JsonObject>();
    jsonRecord["hostname"] = src.hostname;
    jsonRecord["ip"] = src.ip.toString();
    jsonRecord["port"] = src.port;
    return true;
}
void convertFromJson(JsonVariantConst src, MdnsRecord& dst) {
    auto jsonRecord = src.as<JsonObjectConst>();
    dst.hostname = jsonRecord["hostname"].as<String>();
    dst.ip.fromString(jsonRecord["ip"].as<String>());
    dst.port = jsonRecord["port"].as<int>();
}

}}}    // namespace farmhub::kernel::drivers
