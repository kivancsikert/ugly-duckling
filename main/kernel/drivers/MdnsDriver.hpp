#pragma once

#include <ArduinoJson.h>

#include <mdns.h>

#include <kernel/Concurrent.hpp>
#include <kernel/NvsStore.hpp>
#include <kernel/State.hpp>
#include <kernel/Task.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

namespace farmhub::kernel::drivers {

struct MdnsRecord {
    String hostname;
    esp_ip4_addr_t ip;
    int port;

    bool hasHostname() const {
        return !hostname.isEmpty();
    }

    bool hasIp() const {
        return ip.addr != 0;
    }

    bool hasPort() const {
        return port > 0;
    }

    bool validate() const {
        return (hasHostname() || hasIp()) && hasPort();
    }

    String ipAsString() const {
        char ipStr[16];
        esp_ip4addr_ntoa(&ip, ipStr, sizeof(ipStr));
        return ipStr;
    }

    String ipOrHost() const {
        if (hasIp()) {
            return ipAsString();
        } else {
            return hostname;
        }
    }

    String toString() const {
        String result = ipOrHost();
        result += ":" + port;
        return result;
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
        Task::run("mdns:init", 4096, [&wifi, &mdnsReady, instanceName, hostname, version](Task& task) {
            LOGTI("mdns", "initializing");
            WiFiConnection connection(wifi);

            ESP_ERROR_CHECK(mdns_init());

            mdns_hostname_set(hostname.c_str());
            mdns_instance_name_set(instanceName.c_str());
            LOGTD("mdns", "Advertising service %s on %s.local, version: %s",
                instanceName.c_str(), hostname.c_str(), version.c_str());
            mdns_service_add(instanceName.c_str(), "_farmhub", "_tcp", 80, nullptr, 0);
            mdns_txt_item_t txt[] = {
                { "version", version.c_str() },
            };
            mdns_service_txt_set("_farmhub", "_tcp", txt, 1);
            LOGTI("mdns", "configured");

            mdnsReady.set();
        });
    }

    bool lookupService(const String& serviceName, const String& port, MdnsRecord& record, bool loadFromCache = true, milliseconds timeout = 5s) {
        // Wait indefinitely
        Lock lock(lookupMutex);
        auto result = lookupServiceUnderMutex(serviceName, port, record, loadFromCache, timeout);
        return result;
    }

private:
    bool lookupServiceUnderMutex(const String& serviceName, const String& port, MdnsRecord& record, bool loadFromCache, milliseconds timeout) {
        // TODO Use a callback and retry if cached entry doesn't work
        String cacheKey = serviceName + "." + port;
        if (loadFromCache) {
            if (nvs.get(cacheKey, record)) {
                if (record.validate()) {
                    LOGTD("mdns", "found %s in NVS cache: %s",
                        cacheKey.c_str(), record.hostname.c_str());
                    return true;
                } else {
                    LOGTD("mdns", "invalid record in NVS cache for %s, removing",
                        cacheKey.c_str());
                    nvs.remove(cacheKey);
                }
            }
        } else {
            LOGTD("mdns", "removing untrusted record for %s from NVS cache",
                cacheKey.c_str());
            nvs.remove(cacheKey);
        }

        WiFiConnection connection(wifi);
        mdnsReady.awaitSet();

        mdns_result_t* results = nullptr;
        esp_err_t err = mdns_query_ptr(String("_" + serviceName).c_str(), String("_" + port).c_str(), timeout.count(), 1, &results);
        if (err) {
            LOGTE("mdns", "query failed for %s.%s: %d",
                serviceName.c_str(), port.c_str(), err);
            return false;
        }
        if (results == nullptr) {
            LOGTI("mdns", "no results found for %s.%s",
                serviceName.c_str(), port.c_str());
            return false;
        }

        auto& result = *results;
        if (result.hostname != nullptr) {
            record.hostname = result.hostname;
        }
        if (result.addr != nullptr) {
            record.ip = result.addr->addr.u_addr.ip4;
        }
        record.port = result.port;
        mdns_query_results_free(results);

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
    if (src.hasHostname()) {
        jsonRecord["hostname"] = src.hostname;
    }
    if (src.hasIp()) {
        jsonRecord["ip"] = src.ipAsString();
    }
    if (src.hasPort()) {
        jsonRecord["port"] = src.port;
    }
    return true;
}
void convertFromJson(JsonVariantConst src, MdnsRecord& dst) {
    auto jsonRecord = src.as<JsonObjectConst>();
    if (jsonRecord["hostname"].is<String>()) {
        dst.hostname = jsonRecord["hostname"].as<String>();
    } else {
        dst.hostname = "";
    }
    if (jsonRecord["ip"].is<String>()) {
        const char* ipStr = jsonRecord["ip"].as<String>().c_str();
        dst.ip.addr = esp_ip4addr_aton(ipStr);
    } else {
        dst.ip.addr = 0;
    }
    if (jsonRecord["port"].is<int>()) {
        dst.port = jsonRecord["port"].as<int>();
    } else {
        dst.port = 0;
    }
}

}    // namespace farmhub::kernel::drivers
