#pragma once

#include <ConfigEnvelope.hpp>
#include <Manager.hpp>
#include <NvsStore.hpp>
#include <StoredConfig.hpp>
#include <Telemetry.hpp>

#include <functions/FunctionConfigTracker.hpp>
#include <peripherals/Peripheral.hpp>

using cornucopia::ugly_duckling::peripherals::PeripheralManager;

namespace cornucopia::ugly_duckling::functions {

// Fields are kept in alphabetical order — please maintain this when adding new entries.
struct FunctionServices {
    const std::shared_ptr<PeripheralManager> peripherals;
    const std::shared_ptr<TelemetryPublisher> telemetryPublisher;
};

struct FunctionInitParameters {
    template <typename T>
    std::shared_ptr<T> peripheral(const std::string& name) const {
        return services.peripherals->getPeripheral<T>(name);
    }

    const std::string name;
    const FunctionServices& services;
};

struct FunctionCreationResult {
    Handle handle;
    ConfigureFn configureFn;    // empty if the function doesn't implement HasConfig<TConfig>
    std::string fingerprint;    // empty if no config, or none was found in NVS at boot
    std::string requestedAt;    // empty if no config, or none was found in NVS at boot
};

using FunctionCreateFn = std::function<FunctionCreationResult(
    FunctionInitParameters& params,
    const std::shared_ptr<NvsStore>& nvs,
    const std::string& jsonSettings)>;
using FunctionFactory = kernel::Factory<FunctionCreateFn>;

// Helper to build a FunctionFactory while keeping strong types for settings/config
template <
    typename Impl,
    std::derived_from<ConfigurationSection> TSettings,
    std::derived_from<ConfigurationSection> TConfig = EmptyConfiguration,
    typename... TSettingsArgs>
FunctionFactory makeFunctionFactory(
    const std::string& type,
    std::function<std::shared_ptr<Impl>(const FunctionInitParameters&, const std::shared_ptr<TSettings>&)> makeImpl,
    TSettingsArgs... settingsArgs) {
    auto settingsTuple = std::make_tuple(std::forward<TSettingsArgs>(settingsArgs)...);

    // Build the factory using designated initializers (C++20+)
    return FunctionFactory {
        .factoryType = type,
        .productType = std::move(type),
        .create = [settingsTuple, makeImpl = std::move(makeImpl)](
                      FunctionInitParameters& params,
                      const std::shared_ptr<NvsStore>& nvs,
                      const std::string& jsonSettings) -> FunctionCreationResult {
            // Construct and load settings
            auto settings = std::apply([](auto&&... a) {
                return std::make_shared<TSettings>(std::forward<decltype(a)>(a)...);
            },
                settingsTuple);
            settings->loadFromString(jsonSettings);

            constexpr bool hasConfig = std::is_base_of_v<HasConfig<TConfig>, Impl>;

            // We load configuration up front so it's available for the fingerprint/requestedAt below
            // even when the instantiation of the function fails later.
            std::shared_ptr<TConfig> config = std::make_shared<TConfig>();
            std::shared_ptr<StoredConfig> storedConfig;
            if constexpr (hasConfig) {
                storedConfig = std::make_shared<StoredConfig>(nvs, params.name);
                if (storedConfig->hasValue()) {
                    JsonDocument dataCopy = storedConfig->data();
                    config->load(dataCopy.as<JsonObject>());
                }
            }

            // Create concrete implementation via user-provided callable
            auto impl = makeImpl(params, settings);

            FunctionCreationResult result;
            if constexpr (hasConfig) {
                std::static_pointer_cast<HasConfig<TConfig>>(impl)->configure(config);
                if (storedConfig->hasValue()) {
                    result.fingerprint = storedConfig->fingerprint();
                    result.requestedAt = storedConfig->requestedAt();
                }

                result.configureFn = [impl](JsonObjectConst data) {
                    auto parsed = std::make_shared<TConfig>();
                    JsonDocument copy;
                    copy.set(data);
                    parsed->load(copy.as<JsonObject>());
                    std::static_pointer_cast<HasConfig<TConfig>>(impl)->configure(parsed);
                };
            }

            result.handle = Handle::wrap(std::move(impl));
            return result;
        },
    };
}

/**
 * @brief In-memory source of truth for name -> {handle, fingerprint} across every live function.
 *
 * Evolved from FunctionManager per docs/Configuration.md: reconfigure() is the
 * hot-reload entry point that persists a verbatim envelope, parses it, applies it, and records the
 * fingerprint/requestedAt only once configure() succeeds (proof-of-apply, not proof-of-receipt);
 * manifest() reports name -> {fingerprint, requestedAt} straight from this in-memory state for the
 * SYNC builder. The apply-and-track-fingerprint bookkeeping itself lives in FunctionConfigTracker,
 * which has no NVS dependency and is unit-tested directly; this class adds the NVS-touching
 * persistence step.
 * reconfigure() is called for real by the `…/update` handler in Device.hpp's
 * registerUpdateHandler(); persist() is that same handler's device-changed-and-rebooting branch,
 * which needs the envelope written but not applied.
 */
class FunctionRegistry final {
public:
    FunctionRegistry(
        const std::shared_ptr<NvsStore>& nvs,
        const FunctionServices& services)
        : nvs(nvs)
        , services(services)
        , manager("function") {
    }

    bool createFunction(const std::string& functionSettings, JsonArray functionsInitJson) {
        auto initJson = functionsInitJson.add<JsonObject>();
        try {
            manager.createFromSettings(
                functionSettings,
                initJson,
                [&](const std::string& name, const FunctionFactory& factory, const std::string& settings) {
                    FunctionInitParameters params = {
                        .name = name,
                        .services = services,
                    };
                    FunctionCreationResult result = factory.create(params, nvs, settings);
                    tracker.record(name, std::move(result.configureFn), result.fingerprint, result.requestedAt);
                    return result.handle;
                });
            return true;
        } catch (const std::exception& e) {
            LOGE("%s",
                e.what());
            initJson["error"] = std::string(e.what());
            return false;
        }
    }

    // Hot-reload entry point: applies the envelope and records the fingerprint via the tracker,
    // then persists it -- in that order, so a faulty body (configureFn throws) leaves both NVS and
    // the in-memory fingerprint on the last-good envelope, matching the tracker's own
    // proof-of-apply guarantee (record only after configureFn succeeds). Persisting first would
    // leave a broken envelope in NVS even though the in-memory/SYNC fingerprint still (correctly)
    // reports the old one -- and boot would then reload and fail on that same broken envelope every
    // time. Throws on any failure (unknown function name, a function without configuration, or a
    // faulty body) -- there is no rejection reporting in Phase 1, so this is unhandled exactly like
    // any other faulty configuration today.
    void reconfigure(const std::string& name, const ConfigEnvelope& envelope) {
        tracker.apply(name, envelope.getData().template as<JsonObjectConst>(), envelope.getFingerprint(), envelope.getRequestedAt());

        StoredConfig storedConfig(nvs, name);
        storedConfig.store(envelope);
    }

    // Persists an envelope without applying it. Used when a device-configuration change is
    // rebooting anyway -- boot re-derives every live function (and its fingerprint) from NVS, so
    // there is nothing to apply live or track in-memory here.
    void persist(const std::string& name, const ConfigEnvelope& envelope) {
        StoredConfig storedConfig(nvs, name);
        storedConfig.store(envelope);
    }

    // name -> {fingerprint, requestedAt} for every live function, straight from in-memory state --
    // never re-reading NVS, so it reflects what actually applied.
    std::unordered_map<std::string, FunctionManifestEntry> manifest() const {
        return tracker.manifest();
    }

    void registerFactory(FunctionFactory factory) {
        manager.registerFactory(std::move(factory));
    }

    void shutdown() {
        manager.shutdown();
    }

private:
    const std::shared_ptr<NvsStore> nvs;
    const FunctionServices services;

    SettingsBasedManager<FunctionFactory> manager;
    FunctionConfigTracker tracker;
};

}    // namespace cornucopia::ugly_duckling::functions
