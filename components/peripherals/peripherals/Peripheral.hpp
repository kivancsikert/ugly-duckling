#pragma once

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
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

#include <peripherals/api/IPeripheral.hpp>

#include "PeripheralException.hpp"

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals::api;

namespace farmhub::peripherals {

class Peripheral
    : public virtual IPeripheral,
      public Named {
public:
    explicit Peripheral(const std::string& name)
        : Named(name) {
    }

    const std::string& getName() const override {
        return Named::name;
    }
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

struct PeripheralInitParameters;

using PeripheralCreateFn = std::function<Handle(
    PeripheralInitParameters& params,
    const std::shared_ptr<FileSystem>& fs,
    const std::string& jsonSettings,
    JsonObject& initConfigJson)>;
using PeripheralFactory = kernel::Factory<PeripheralCreateFn>;

struct PeripheralInitParameters {
    void registerFeature(const std::string& type, std::function<void(JsonObject&)> populate) {
        telemetryCollector->registerFeature(type, name, std::move(populate));
        features.add(type);
    }

    template <typename T>
    std::shared_ptr<T> peripheral(const std::string& name) const {
        return peripherals.getInstance<T>(name);
    }

    const std::string name;
    const std::shared_ptr<MqttRoot> mqttRoot;
    const PeripheralServices& services;
    const std::shared_ptr<TelemetryCollector> telemetryCollector;
    const JsonArray features;

    Manager<PeripheralFactory>& peripherals;
};

// Helper to build a PeripheralFactory while keeping strong types for settings/config
template <
    typename Type,
    std::derived_from<Type> Impl,
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
                      JsonObject& initConfigJson) -> Handle {
            // Construct and load settings
            auto settings = std::apply([](auto&&... a) {
                return std::make_shared<TSettings>(std::forward<decltype(a)>(a)...);
            },
                settingsTuple);
            settings->loadFromString(jsonSettings);

            constexpr bool hasConfig = std::is_base_of_v<HasConfig<TConfig>, Impl>;

            // We load configuration up front to ensure that we always store it in the init message, even
            // when the instantiation of the peripheral fails later.
            auto config = std::make_shared<TConfig>();
            std::shared_ptr<ConfigurationFile<TConfig>> configFile;
            if constexpr (hasConfig) {
                configFile = std::make_shared<ConfigurationFile<TConfig>>(fs, "/p/" + params.name, config);
                // Store configuration in init message
                config->store(initConfigJson);
            }

            // Create concrete implementation via user-provided callable
            auto impl = makeImpl(params, settings);

            // Configuration lifecycle, mirroring the templated factory behavior
            if constexpr (hasConfig) {
                std::static_pointer_cast<HasConfig<TConfig>>(impl)->configure(config);

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
            }
            return Handle::wrap(std::move(std::static_pointer_cast<Type>(impl)));
        },
    };
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
        , mqttDeviceRoot(mqttDeviceRoot)
        , manager("peripheral") {
    }

    bool createPeripheral(const std::string& peripheralSettings, JsonArray peripheralsInitJson) {
        auto initJson = peripheralsInitJson.add<JsonObject>();
        try {
            manager.createFromSettings(
                peripheralSettings,
                initJson,
                [&](const std::string& name, const PeripheralFactory& factory, const std::string& settings) {
                    PeripheralInitParameters params = {
                        .name = name,
                        .mqttRoot = mqttDeviceRoot->forSuffix("peripherals/" + factory.productType + "/" + name),
                        .services = services,
                        .telemetryCollector = telemetryCollector,
                        .features = initJson["features"].to<JsonArray>(),
                        .peripherals = manager,
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

    void registerFactory(PeripheralFactory factory) {
        manager.registerFactory(std::move(factory));
    }

    template <typename T>
    std::shared_ptr<T> getPeripheral(const std::string& name) const {
        return manager.getInstance<T>(name);
    }

    void shutdown() {
        manager.shutdown();
    }

private:
    const std::shared_ptr<FileSystem> fs;
    const std::shared_ptr<TelemetryCollector> telemetryCollector;
    const PeripheralServices services;
    const std::shared_ptr<MqttRoot> mqttDeviceRoot;

    SettingsBasedManager<PeripheralFactory> manager;
};

}    // namespace farmhub::peripherals
