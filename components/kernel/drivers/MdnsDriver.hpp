#pragma once

#include <ArduinoJson.h>

#include <mdns.h>

#include <Concurrent.hpp>
#include <NvsStore.hpp>
#include <State.hpp>
#include <Task.hpp>

namespace farmhub::kernel::drivers {

struct MdnsRecord {
    std::string hostname;
    esp_ip4_addr_t ip;
    int port;

    bool hasHostname() const {
        return !hostname.empty();
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

    std::string ipAsString() const {
        char ipStr[16];
        esp_ip4addr_ntoa(&ip, ipStr, sizeof(ipStr));
        return ipStr;
    }

    std::string ipOrHost() const {
        if (hasIp()) {
            return ipAsString();
        }
        return hostname;
    }

    std::string toString() const {
        std::string result = ipOrHost();
        result += ":";
        result += std::to_string(port);
        return result;
    }
};

class MdnsDriver {
public:
    MdnsDriver(
        State& networkReady,
        const std::string& hostname,
        const std::string& instanceName,
        const std::string& version,
        StateSource& mdnsReady)
        : networkReady(networkReady)
        , mdnsReady(mdnsReady) {
        // TODO Add error handling
        Task::run("mdns:init", 4096, [networkReady, mdnsReady, instanceName, hostname, version](Task& task) {
            LOGTI(Tag::MDNS, "initializing");
            networkReady.awaitSet();

            ESP_ERROR_CHECK(mdns_init());

            mdns_hostname_set(hostname.c_str());
            mdns_instance_name_set(instanceName.c_str());
            LOGTD(Tag::MDNS, "Advertising service %s on %s.local, version: %s",
                instanceName.c_str(), hostname.c_str(), version.c_str());
            mdns_service_add(instanceName.c_str(), "_farmhub", "_tcp", 80, nullptr, 0);
            mdns_txt_item_t txt[] = {
                { "version", version.c_str() },
            };
            mdns_service_txt_set("_farmhub", "_tcp", txt, 1);
            LOGTI(Tag::MDNS, "configured");

            mdnsReady.set();
        });
    }

    bool lookupService(const std::string& serviceName, const std::string& port, MdnsRecord& record, bool loadFromCache = true, milliseconds timeout = 5s) {
        // Wait indefinitely
        Lock lock(lookupMutex);
        auto result = lookupServiceUnderMutex(serviceName, port, record, loadFromCache, timeout);
        return result;
    }

    State& getMdnsReady() {
        return mdnsReady;
    }

private:
    bool lookupServiceUnderMutex(const std::string& serviceName, const std::string& port, MdnsRecord& record, bool loadFromCache, milliseconds timeout) {
        // TODO Use a callback and retry if cached entry doesn't work
        std::string cacheKey = serviceName + "." + port;
        if (loadFromCache) {
            if (nvs.get(cacheKey, record)) {
                if (record.validate()) {
                    LOGTD(Tag::MDNS, "found %s in NVS cache: %s",
                        cacheKey.c_str(), record.hostname.c_str());
                    return true;
                }
                LOGTD(Tag::MDNS, "invalid record in NVS cache for %s, removing",
                    cacheKey.c_str());
                nvs.remove(cacheKey);
            }
        } else {
            LOGTD(Tag::MDNS, "removing untrusted record for %s from NVS cache",
                cacheKey.c_str());
            nvs.remove(cacheKey);
        }

        networkReady.awaitSet();
        mdnsReady.awaitSet();

        mdns_result_t* results = nullptr;
        esp_err_t err = mdns_query_ptr(std::string("_" + serviceName).c_str(), std::string("_" + port).c_str(), timeout.count(), 1, &results);
        if (err != 0) {
            LOGTE(Tag::MDNS, "query failed for %s.%s: %d",
                serviceName.c_str(), port.c_str(), err);
            return false;
        }
        if (results == nullptr) {
            LOGTI(Tag::MDNS, "no results found for %s.%s",
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

    State& networkReady;

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
    if (jsonRecord["hostname"].is<std::string>()) {
        dst.hostname = jsonRecord["hostname"].as<std::string>();
    } else {
        dst.hostname = "";
    }
    if (jsonRecord["ip"].is<std::string>()) {
        const char* ipStr = jsonRecord["ip"].as<const char*>();
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
