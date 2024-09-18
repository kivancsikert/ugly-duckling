#pragma once

#include <ArduinoJson.h>
#include <ESPmDNS.h>

#include <kernel/Concurrent.hpp>
#include <kernel/Log.hpp>
#include <kernel/NvsStore.hpp>
#include <kernel/State.hpp>
#include <kernel/Task.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

namespace farmhub::kernel::drivers {

struct MdnsRecord {
    String hostname;
    IPAddress ip;
    int port;

    bool validate() {
        return hostname.length() > 0 && ip != IPAddress() && port > 0;
    }
};

class MdnsDriver {
public:
    MdnsDriver(
        WiFiDriver& wifi,
        const String& hostname,
        const String& instanceName,
        const String& version,
        StateSource& mdnsReady)
        : wifi(wifi)
        , mdnsReady(mdnsReady) {
        // TODO Add error handling
        Task::run("mdns", 4096, [&wifi, &mdnsReady, instanceName, hostname, version](Task& task) {
            Log.info("mDNS: initializing");
            WiFiConnection connection(wifi);

            MDNS.begin(hostname);
            MDNS.setInstanceName(instanceName);
            Log.debug("mDNS: Advertising service %s on %s.local, version: %s",
                instanceName.c_str(), hostname.c_str(), version.c_str());
            MDNS.addService("farmhub", "tcp", 80);
            MDNS.addServiceTxt("farmhub", "tcp", "version", version);
            Log.info("mDNS: configured");

            mdnsReady.set();
        });
    }

    bool lookupService(const String& serviceName, const String& port, MdnsRecord& record, bool loadFromCache = true) {
        // Wait indefinitely
        Lock lock(lookupMutex);
        auto result = lookupServiceUnderMutex(serviceName, port, record, loadFromCache);
        return result;
    }

private:
    bool lookupServiceUnderMutex(const String& serviceName, const String& port, MdnsRecord& record, bool loadFromCache) {
        // TODO Use a callback and retry if cached entry doesn't work
        String cacheKey = serviceName + "." + port;
        if (loadFromCache) {
            if (nvs.get(cacheKey, record)) {
                if (record.validate()) {
                    Log.debug("mDNS: found %s in NVS cache: %s",
                        cacheKey.c_str(), record.hostname.c_str());
                    return true;
                } else {
                    Log.debug("mDNS: invalid record in NVS cache for %s, removing",
                        cacheKey.c_str());
                    nvs.remove(cacheKey);
                }
            }
        } else {
            Log.debug("mDNS: removing untrusted record for %s from NVS cache",
                cacheKey.c_str());
            nvs.remove(cacheKey);
        }

        WiFiConnection connection(wifi);
        mdnsReady.awaitSet();
        auto count = MDNS.queryService(serviceName.c_str(), port.c_str());
        if (count == 0) {
            return false;
        }
        Log.info("mDNS: found %d services, choosing first:",
            count);
        for (int i = 0; i < count; i++) {
            Log.info(" %s%d) %s:%d (%s)",
                i == 0 ? "*" : " ",
                i + 1,
                MDNS.hostname(i).c_str(),
                MDNS.port(i),
                MDNS.IP(i).toString().c_str());
        }
        record.hostname = MDNS.hostname(0);
        record.ip = MDNS.IP(0);
        record.port = MDNS.port(0);

        nvs.set(cacheKey, record);

        return true;
    }

    WiFiDriver& wifi;

    State& mdnsReady;

    Mutex lookupMutex;

    NvsStore nvs { "mdns" };
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

}    // namespace farmhub::kernel::drivers
