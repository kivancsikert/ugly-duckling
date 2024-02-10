#pragma once

#include <ArduinoJson.h>
#include <ESPmDNS.h>

#include <ArduinoLog.h>

#include <kernel/Concurrent.hpp>
#include <kernel/NvmStore.hpp>
#include <kernel/State.hpp>
#include <kernel/Task.hpp>

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
        State& networkReady,
        const String& hostname,
        const String& instanceName,
        const String& version,
        StateSource& mdnsReady)
        : mdnsReady(mdnsReady) {
        // TODO Add error handling
        MDNS.begin(hostname);
        Task::run("mdns", 4096, [&networkReady, &mdnsReady, instanceName, hostname, version](Task& task) {
            networkReady.awaitSet();

            MDNS.setInstanceName(instanceName);
            Log.traceln("mDNS: Advertising service %s on %s.local, version: %s",
                instanceName.c_str(), hostname.c_str(), version.c_str());
            MDNS.addService("farmhub", "tcp", 80);
            MDNS.addServiceTxt("farmhub", "tcp", "version", version);
            Log.infoln("mDNS: configured");

            mdnsReady.set();
        });
    }

    bool lookupService(const String& serviceName, const String& port, MdnsRecord& record, bool loadFromCache = true) {
        // Wait indefinitely
        lookupMutex.lock();
        auto result = lookupServiceUnderMutex(serviceName, port, record, loadFromCache);
        lookupMutex.unlock();
        return result;
    }

private:
    bool lookupServiceUnderMutex(const String& serviceName, const String& port, MdnsRecord& record, bool loadFromCache) {
        // TODO Use a callback and retry if cached entry doesn't work
        String cacheKey = serviceName + "." + port;
        if (loadFromCache) {
            if (nvm.get(cacheKey, record)) {
                if (record.validate()) {
                    Log.traceln("mDNS: found %s in NVM cache: %s",
                        cacheKey.c_str(), record.hostname.c_str());
                    return true;
                } else {
                    Log.traceln("mDNS: invalid record in NVM cache for %s, removing",
                        cacheKey.c_str());
                    nvm.remove(cacheKey);
                }
            }
        } else {
            Log.traceln("mDNS: removing untrusted record for %s from NVM cache",
                cacheKey.c_str());
            nvm.remove(cacheKey);
        }

        mdnsReady.awaitSet();
        auto count = MDNS.queryService(serviceName.c_str(), port.c_str());
        if (count == 0) {
            return false;
        }
        Log.infoln("mDNS: found %d services, choosing first:",
            count);
        for (int i = 0; i < count; i++) {
            Log.infoln(" %s%d) %s:%d (%s)",
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

    State& mdnsReady;

    Mutex lookupMutex;

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

}    // namespace farmhub::kernel::drivers
