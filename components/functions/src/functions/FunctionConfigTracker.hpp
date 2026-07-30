#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <ArduinoJson.h>

namespace cornucopia::ugly_duckling::functions {

// Applies a verbatim config body (already parsed out of a ConfigEnvelope) to a live function
// instance. Empty for functions that don't implement HasConfig<TConfig>.
using ConfigureFn = std::function<void(JsonObjectConst)>;

/**
 * @brief In-memory name -> {configureFn, fingerprint} bookkeeping at the heart of FunctionRegistry
 * (docs/specs/config-reconciliation.md). Deliberately has no NVS/MQTT dependency of its own, so the
 * apply-and-track-fingerprint logic is unit-testable with a fake configureFn, independent of
 * FunctionRegistry's real persistence (StoredConfig, which needs real NVS).
 */
class FunctionConfigTracker {
public:
    void record(const std::string& name, ConfigureFn configureFn, const std::string& fingerprint) {
        entries[name] = Entry { .configureFn = std::move(configureFn), .fingerprint = fingerprint };
    }

    // Applies data via the named entry's configureFn and records the fingerprint only once
    // configureFn succeeds (proof-of-apply, not proof-of-receipt). Throws if the name is unknown or
    // was recorded without a configureFn -- a protocol violation, unhandled in Phase 1 exactly like
    // any other faulty configuration.
    void apply(const std::string& name, JsonObjectConst data, const std::string& fingerprint) {
        auto it = entries.find(name);
        if (it == entries.end()) {
            throw std::runtime_error("Cannot reconfigure unknown function '" + name + "'");
        }
        Entry& entry = it->second;
        if (!entry.configureFn) {
            throw std::runtime_error("Function '" + name + "' does not support configuration");
        }
        entry.configureFn(data);
        entry.fingerprint = fingerprint;
    }

    // name -> fingerprint for every recorded entry, straight from in-memory state.
    std::unordered_map<std::string, std::string> manifest() const {
        std::unordered_map<std::string, std::string> result;
        for (const auto& [name, entry] : entries) {
            result.emplace(name, entry.fingerprint);
        }
        return result;
    }

private:
    struct Entry {
        ConfigureFn configureFn;
        std::string fingerprint;
    };

    std::unordered_map<std::string, Entry> entries;
};

}    // namespace cornucopia::ugly_duckling::functions
