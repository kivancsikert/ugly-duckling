#pragma once

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>

#include <BootClock.hpp>
#include <Configuration.hpp>
#include <EspException.hpp>
#include <I2CManager.hpp>
#include <Manager.hpp>
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

using Peripheral = kernel::Handle;

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
    // Allows factories to register a shutdown callback with the manager
    std::function<void(std::function<void(const ShutdownParameters&)>)> registerShutdown;
};

// Use the generic kernel::Factory for peripherals
using PeripheralCreateFn = std::function<Peripheral(
    PeripheralInitParameters& params,
    const std::shared_ptr<FileSystem>& fs,
    const std::string& jsonSettings,
    JsonObject& initConfigJson)>;
using PeripheralFactory = kernel::Factory<Peripheral, PeripheralCreateFn>;

// Internal helpers to constrain factory callables
template <typename T>
struct is_shared_ptr : std::false_type { };
template <typename T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type { };

// Helper to build a PeripheralFactory while keeping strong types for settings/config
template <
    typename Impl,
    std::derived_from<ConfigurationSection> TSettings,
    std::derived_from<ConfigurationSection> TConfig = EmptyConfiguration,
    typename... TSettingsArgs>
PeripheralFactory makePeripheralFactory(const std::string& factoryType,
    const std::string& peripheralType,
    std::function<std::shared_ptr<Impl>(PeripheralInitParameters&, const std::shared_ptr<TSettings>&)> makeImpl,
    TSettingsArgs... settingsArgs) {
    auto settingsTuple = std::make_tuple(std::forward<TSettingsArgs>(settingsArgs)...);

    // Build the factory using designated initializers (C++20+)
    auto effectiveType = peripheralType.empty() ? factoryType : peripheralType;
    return PeripheralFactory {
        .factoryType = std::move(factoryType),
        .productType = std::move(effectiveType),
        .create = [settingsTuple, makeImpl = std::move(makeImpl)](
                      PeripheralInitParameters& params,
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

            // If implementation supports shutdown, register it with the manager now
            if constexpr (std::is_base_of_v<HasShutdown, Impl>) {
                params.registerShutdown([impl](const ShutdownParameters& p) {
                    std::static_pointer_cast<HasShutdown>(impl)->shutdown(p);
                });
            }

            return Peripheral::wrap(std::move(impl));
        },
    };
}

// Peripheral manager

class PeripheralManager final : public kernel::SettingsBasedManager<Peripheral, PeripheralFactory> {
public:
    PeripheralManager(
        const std::shared_ptr<FileSystem>& fs,
        const std::shared_ptr<TelemetryCollector>& telemetryCollector,
        PeripheralServices services,
        const std::shared_ptr<MqttRoot>& mqttDeviceRoot)
        : kernel::SettingsBasedManager<Peripheral, PeripheralFactory>("peripheral")
        , fs(fs)
        , telemetryCollector(telemetryCollector)
        , services(std::move(services))
        , mqttDeviceRoot(mqttDeviceRoot) {
    }

    bool createPeripheral(const std::string& peripheralSettings, JsonArray peripheralsInitJson) {
        if (state == State::Stopped) {
            LOGE("Not creating peripherals because the peripheral manager is stopped");
            return false;
        }

        auto initJson = peripheralsInitJson.add<JsonObject>();
        try {
            createFromSettings(
                peripheralSettings,
                initJson,
                [&](const std::string& name, const std::string& settings, const PeripheralFactory& factory) {
                    PeripheralInitParameters params = {
                        .name = name,
                        .mqttRoot = mqttDeviceRoot->forSuffix("peripherals/" + factory.productType + "/" + name),
                        .services = services,
                        .telemetryCollector = telemetryCollector,
                        .features = initJson["features"].to<JsonArray>(),
                        .registerShutdown = [this, name](std::function<void(const ShutdownParameters&)> cb) {
                            shutdownCallbacks.emplace_back(name, std::move(cb));
                        },
                    };
                    JsonObject initConfigJson = initJson["config"].to<JsonObject>();
                    return factory.create(params, fs, settings, initConfigJson);
                });
            return true;
        } catch (const std::exception& e) {
            LOGE("%s",
                e.what());
            initJson["error"] = std::string(e.what());
            return false;
        }
    }

    void shutdown() {
        Lock lock(getMutex());
        if (state == State::Stopped) {
            LOGD("Peripheral manager is already stopped");
            return;
        }
        LOGI("Shutting down peripheral manager");
        state = State::Stopped;
        ShutdownParameters parameters = {};
        for (auto& [name, cb] : shutdownCallbacks) {
            LOGI("Shutting down peripheral '%s'", name.c_str());
            try {
                cb(parameters);
            } catch (const std::exception& e) {
                LOGE("Shutdown of peripheral '%s' threw: %s", name.c_str(), e.what());
            }
        }
    }

private:
    enum class State : uint8_t {
        Running,
        Stopped
    };

    const std::shared_ptr<FileSystem> fs;
    const std::shared_ptr<TelemetryCollector> telemetryCollector;
    const PeripheralServices services;
    const std::shared_ptr<MqttRoot> mqttDeviceRoot;

    State state = State::Running;
    std::vector<std::pair<std::string, std::function<void(const ShutdownParameters&)>>> shutdownCallbacks;
};

}    // namespace farmhub::peripherals
