#pragma once

#include <string>

#include <ArduinoJson.h>

namespace cornucopia::ugly_duckling::kernel {

/**
 * @brief A verbatim configuration envelope: the config body plus the opaque fingerprint and
 * requestedAt stamp it arrived with. The firmware never hashes or reinterprets any of these --
 * data, fingerprint, and requestedAt are stored and echoed back exactly as received from the
 * authority (see docs/specs/config-reconciliation.md, "Storage").
 */
class ConfigEnvelope {
public:
    ConfigEnvelope() = default;

    ConfigEnvelope(JsonVariantConst data, const std::string& fingerprint, const std::string& requestedAt)
        : fingerprint(fingerprint)
        , requestedAt(requestedAt) {
        this->data.set(data);
    }

    const JsonDocument& getData() const {
        return data;
    }

    const std::string& getFingerprint() const {
        return fingerprint;
    }

    const std::string& getRequestedAt() const {
        return requestedAt;
    }

private:
    JsonDocument data;
    std::string fingerprint;
    std::string requestedAt;
};

}    // namespace cornucopia::ugly_duckling::kernel

namespace ArduinoJson {

using cornucopia::ugly_duckling::kernel::ConfigEnvelope;

template <>
struct Converter<ConfigEnvelope> {
    static bool toJson(const ConfigEnvelope& src, JsonVariant dst) {
        JsonObject obj = dst.to<JsonObject>();
        obj["data"] = src.getData();
        obj["fingerprint"] = src.getFingerprint();
        obj["requestedAt"] = src.getRequestedAt();
        return true;
    }

    static ConfigEnvelope fromJson(JsonVariantConst src) {
        return { src["data"], src["fingerprint"] | std::string(), src["requestedAt"] | std::string() };
    }

    static bool checkJson(JsonVariantConst src) {
        return src["data"].is<JsonObjectConst>();
    }
};

}    // namespace ArduinoJson
