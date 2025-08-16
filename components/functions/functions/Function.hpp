#pragma once

#include <FileSystem.hpp>
#include <Manager.hpp>

#include <peripherals/Peripheral.hpp>

using farmhub::peripherals::PeripheralManager;

namespace farmhub::functions {

using Function = kernel::Handle;

struct FunctionInitParameters {
    const std::string name;
    const std::shared_ptr<PeripheralManager> peripherals;
    const std::shared_ptr<MqttRoot> mqttRoot;
};

using FunctionCreateFn = std::function<Function(
    FunctionInitParameters& params,
    const std::shared_ptr<FileSystem>& fs,
    const std::string& jsonSettings,
    JsonObject& initConfigJson)>;
using FunctionFactory = kernel::Factory<Function, FunctionCreateFn>;

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
                      const std::shared_ptr<FileSystem>& fs,
                      const std::string& jsonSettings,
                      JsonObject& initConfigJson) -> Function {
            // Construct and load settings
            auto settings = std::apply([](auto&&... a) {
                return std::make_shared<TSettings>(std::forward<decltype(a)>(a)...);
            },
                settingsTuple);
            settings->loadFromString(jsonSettings);

            // Create concrete implementation via user-provided callable
            auto impl = makeImpl(params, settings);

            // Configuration lifecycle, mirroring the templated factory behavior
            if constexpr (std::is_base_of_v<HasConfig<TConfig>, Impl>) {
                auto config = std::make_shared<TConfig>();
                auto configFile = std::make_shared<ConfigurationFile<TConfig>>(fs, "/f/" + params.name, config);
                std::static_pointer_cast<HasConfig<TConfig>>(impl)->configure(config);
                // Store configuration in init message
                config->store(initConfigJson, false);

                // Subscribe for config updates
                params.mqttRoot->subscribe("config", [name = params.name, configFile, impl](const std::string&, const JsonObject& cfgJson) {
                    LOGD("Received configuration update for function: %s", name.c_str());
                    try {
                        configFile->update(cfgJson);
                        if constexpr (std::is_base_of_v<HasConfig<TConfig>, Impl>) {
                            std::static_pointer_cast<HasConfig<TConfig>>(impl)->configure(configFile->getConfig());
                        }
                    } catch (const std::exception& e) {
                        LOGE("Failed to update configuration for function '%s' because %s", name.c_str(), e.what());
                    }
                });
            }

            return Function::wrap(std::move(impl));
        },
    };
}

class FunctionManager : public kernel::SettingsBasedManager<Function, FunctionFactory> {
public:
    FunctionManager(
        const std::shared_ptr<FileSystem>& fs,
        const std::shared_ptr<PeripheralManager>& peripherals,
        const std::shared_ptr<MqttRoot>& mqttDeviceRoot)
        : kernel::SettingsBasedManager<Function, FunctionFactory>("function")
        , fs(fs)
        , peripherals(peripherals)
        , mqttDeviceRoot(mqttDeviceRoot) {
    }

    bool createFunction(const std::string& functionSettings, JsonArray functionsInitJson) {
        auto initJson = functionsInitJson.add<JsonObject>();
        try {
            createFromSettings(
                functionSettings,
                initJson,
                [&](const std::string& name, const FunctionFactory& factory, const std::string& settings) {
                    FunctionInitParameters params = {
                        .name = name,
                        .peripherals = peripherals,
                        .mqttRoot = mqttDeviceRoot->forSuffix("functions/" + name),
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

private:
    const std::shared_ptr<FileSystem>& fs;
    const std::shared_ptr<PeripheralManager>& peripherals;
    const std::shared_ptr<MqttRoot>& mqttDeviceRoot;
};

}    // namespace farmhub::functions
