#pragma once

#include <functional>
#include <map>
#include <memory>
#include <tuple>
#include <type_traits>

#include <BootClock.hpp>
#include <Configuration.hpp>
#include <EspException.hpp>
#include <I2CManager.hpp>
#include <PcntManager.hpp>
#include <PulseCounter.hpp>
#include <PwmManager.hpp>
#include <Telemetry.hpp>
#include <drivers/SwitchManager.hpp>
#include <utility>

#include "PeripheralException.hpp"

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

namespace farmhub::peripherals {

// Peripherals

// Forward declarations and common types
struct ShutdownParameters {
    // Placeholder for future parameters
};

// Explicit shutdown capability for implementations that support graceful shutdown
class HasShutdown {
public:
    virtual ~HasShutdown() = default;
    virtual void shutdown(const ShutdownParameters& params) = 0;
};

// Peripheral instance wrapper to allow heterogeneous storage without inheritance
class Peripheral final {
public:
    Peripheral() = default;

    template <typename ImplPtr>
    static Peripheral wrap(std::string name, const ImplPtr& impl) {
        Peripheral p;
        p.name = std::move(name);
        // Keep the implementation alive via shared_ptr<void>
        p._holder = std::static_pointer_cast<void>(impl);
        // Bind shutdown if available via HasShutdown; otherwise, no-op
        p._shutdown = [impl](const ShutdownParameters& params) {
            using Impl = std::remove_reference_t<decltype(*impl)>;
            if constexpr (std::is_base_of_v<HasShutdown, Impl>) {
                std::static_pointer_cast<HasShutdown>(impl)->shutdown(params);
            }
        };
        return p;
    }

    void shutdown(const ShutdownParameters& params) const {
        if (_shutdown) {
            _shutdown(params);
        }
    }

    std::string name;

private:
    std::shared_ptr<void> _holder;
    std::function<void(const ShutdownParameters&)> _shutdown;
};

// Peripheral factories

struct PeripheralServices {
    const std::shared_ptr<I2CManager> i2c;
    const std::shared_ptr<PcntManager> pcntManager;
    const std::shared_ptr<PulseCounterManager> pulseCounterManager;
    const std::shared_ptr<PwmManager> pwmManager;
    const std::shared_ptr<SwitchManager> switches;
    const std::shared_ptr<TelemetryPublisher> telemetryPublisher;
};

struct PeripheralInitParameters {
    void registerFeature(const std::string& type, std::function<void(JsonObject&)> populate) {
        telemetryCollector->registerFeature(type, name, std::move(populate));
        features.add(type);
    }

    const std::string name;
    const std::shared_ptr<MqttRoot> mqttRoot;
    const PeripheralServices& services;
    const std::shared_ptr<TelemetryCollector> telemetryCollector;
    const JsonArray features;
};

// A non-templated factory representation producing peripheral instances
struct PeripheralFactory {
    // Identifiers
    std::string factoryType;
    std::string peripheralType;    // Usually same as factoryType, but can differ

    // Creator function: returns a Peripheral
    std::function<Peripheral(
        PeripheralInitParameters& params,
        const std::shared_ptr<FileSystem>& fs,
        const std::string& jsonSettings,
        JsonObject& initConfigJson)>
        create;
};

// Internal helpers to constrain factory callables
template <typename T>
struct is_shared_ptr : std::false_type { };
template <typename T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type { };

template <typename F, typename TSettings>
concept MakeImplProducesSharedPtr = requires(F f, PeripheralInitParameters& p, std::shared_ptr<TSettings> s) {
    typename std::invoke_result_t<F, PeripheralInitParameters&, std::shared_ptr<TSettings>>;
    requires is_shared_ptr<std::invoke_result_t<F, PeripheralInitParameters&, std::shared_ptr<TSettings>>>::value;
};

// Helper to build a PeripheralFactory while keeping strong types for settings/config
template <
    std::derived_from<ConfigurationSection> TSettings,
    std::derived_from<ConfigurationSection> TConfig = EmptyConfiguration,
    typename MakeImpl,
    typename... TSettingsArgs>
PeripheralFactory makePeripheralFactory(std::string factoryType,
    std::string peripheralType,
    MakeImpl makeImpl,
    TSettingsArgs... settingsArgs) {
    static_assert(MakeImplProducesSharedPtr<MakeImpl, TSettings>,
        "makeImpl must be callable with (PeripheralInitParameters&, std::shared_ptr<TSettings>) and return std::shared_ptr<Impl>");
    auto settingsTuple = std::make_tuple(std::forward<TSettingsArgs>(settingsArgs)...);

    PeripheralFactory f;
    f.factoryType = std::move(factoryType);
    f.peripheralType = peripheralType.empty() ? f.factoryType : std::move(peripheralType);
    f.create = [settingsTuple, makeImpl = std::move(makeImpl)](auto& params,
                   const std::shared_ptr<FileSystem>& fs,
                   const std::string& jsonSettings,
                   JsonObject& initConfigJson) -> Peripheral {
        // Construct and load settings
        auto settings = std::apply([](auto&&... a) {
            return std::make_shared<TSettings>(std::forward<decltype(a)>(a)...);
        },
            settingsTuple);
        settings->loadFromString(jsonSettings);

        // Create concrete implementation via user-provided callable
        auto impl = makeImpl(params, settings);

        // Configuration lifecycle, mirroring the templated factory behavior
        auto config = std::make_shared<TConfig>();
        auto configFile = std::make_shared<ConfigurationFile<TConfig>>(fs, "/p/" + params.name, config);
        using Impl = std::remove_reference_t<decltype(*impl)>;
        if constexpr (std::is_base_of_v<HasConfig<TConfig>, Impl>) {
            std::static_pointer_cast<HasConfig<TConfig>>(impl)->configure(config);
        }
        // Store configuration in init message
        config->store(initConfigJson, false);

        // Subscribe for config updates
        params.mqttRoot->subscribe("config", [name = params.name, configFile, impl](const std::string&, const JsonObject& cfgJson) {
            LOGD("Received configuration update for peripheral: %s", name.c_str());
            try {
                configFile->update(cfgJson);
                if constexpr (std::is_base_of_v<HasConfig<TConfig>, Impl>) {
                    std::static_pointer_cast<HasConfig<TConfig>>(impl)->configure(configFile->getConfig());
                }
            } catch (const std::exception& e) {
                LOGE("Failed to update configuration for peripheral '%s' because %s", name.c_str(), e.what());
            }
        });

        return Peripheral::wrap(params.name, std::move(impl));
    };
    return f;
}

// Peripheral manager

class PeripheralManager final {
public:
    PeripheralManager(
        const std::shared_ptr<FileSystem>& fs,
        const std::shared_ptr<TelemetryCollector>& telemetryCollector,
        PeripheralServices services,
        const std::shared_ptr<MqttRoot>& mqttDeviceRoot)
        : fs(fs)
        , telemetryCollector(telemetryCollector)
        , services(std::move(services))
        , mqttDeviceRoot(mqttDeviceRoot) {
    }

    void registerFactory(PeripheralFactory factory) {
        LOGD("Registering peripheral factory: %s",
            factory.factoryType.c_str());
        factories.emplace(factory.factoryType, std::move(factory));
    }

    bool createPeripheral(const std::string& peripheralSettings, JsonArray peripheralsInitJson) {
        LOGI("Creating peripheral with settings: %s",
            peripheralSettings.c_str());
        std::shared_ptr<PeripheralSettings> settings = std::make_shared<PeripheralSettings>();
        try {
            settings->loadFromString(peripheralSettings);
        } catch (const std::exception& e) {
            LOGE("Failed to parse peripheral settings because %s:\n%s",
                e.what(), peripheralSettings.c_str());
            return false;
        }

        std::string nameFromSettings = settings->name.get();
        std::string factoryType = settings->type.get();
        const auto& nameBeforeCreation = nameFromSettings.empty() ? factoryType : nameFromSettings;

        Lock lock(stateMutex);
        if (state == State::Stopped) {
            LOGE("Not creating peripheral '%s' because the peripheral manager is stopped",
                nameBeforeCreation.c_str());
            return false;
        }

        auto initJson = peripheralsInitJson.add<JsonObject>();
        initJson["factory"] = factoryType;

        LOGD("Creating peripheral '%s' with factory '%s'",
            nameBeforeCreation.c_str(), factoryType.c_str());

        const auto it = factories.find(factoryType);
        if (it == factories.end()) {
            LOGE("Failed to create '%s' peripheral '%s' because factory not found",
                factoryType.c_str(), nameBeforeCreation.c_str());
            initJson["error"] = "Factory not found: '" + factoryType + "'";
            return false;
        }

        const std::string& peripheralType = it->second.peripheralType;
        initJson["type"] = peripheralType;
        const auto& name = nameFromSettings.empty() ? peripheralType : nameFromSettings;
        initJson["name"] = name;
        settings->params.store(initJson, true);

        try {
            PeripheralInitParameters params = {
                .name = name,
                .mqttRoot = mqttDeviceRoot->forSuffix("peripherals/" + peripheralType + "/" + name),
                .services = services,
                .telemetryCollector = telemetryCollector,
                .features = initJson["features"].to<JsonArray>(),
            };
            JsonObject initConfigJson = initJson["config"].to<JsonObject>();
            Peripheral peripheral = it->second.create(params, fs, settings->params.get().get(), initConfigJson);
            peripherals.push_back(std::move(peripheral));

            return true;
        } catch (const std::exception& e) {
            LOGE("Failed to create '%s' peripheral '%s' because %s",
                factoryType.c_str(), name.c_str(), e.what());
            initJson["error"] = std::string(e.what());
            return false;
        } catch (...) {
            LOGE("Failed to create '%s' peripheral '%s' because of an unknown exception",
                factoryType.c_str(), name.c_str());
            initJson["error"] = "unknown exception";
            return false;
        }
    }

    void shutdown() {
        Lock lock(stateMutex);
        if (state == State::Stopped) {
            LOGD("Peripheral manager is already stopped");
            return;
        }
        LOGI("Shutting down peripheral manager");
        state = State::Stopped;
        ShutdownParameters parameters = {};
        for (auto& peripheral : peripherals) {
            LOGI("Shutting down peripheral '%s'",
                peripheral.name.c_str());
            peripheral.shutdown(parameters);
        }
    }

private:
    class PeripheralSettings : public ConfigurationSection {
    public:
        Property<std::string> name { this, "name" };
        Property<std::string> type { this, "type" };
        Property<JsonAsString> params { this, "params" };
    };

    enum class State : uint8_t {
        Running,
        Stopped
    };

    const std::shared_ptr<FileSystem> fs;
    const std::shared_ptr<TelemetryCollector> telemetryCollector;
    const PeripheralServices services;
    const std::shared_ptr<MqttRoot> mqttDeviceRoot;

    // TODO Use an unordered_map?
    std::map<std::string, PeripheralFactory> factories;
    Mutex stateMutex;
    State state = State::Running;
    std::list<Peripheral> peripherals;
};

}    // namespace farmhub::peripherals
