#pragma once

#include <map>
#include <memory>

#include <BootClock.hpp>
#include <Configuration.hpp>
#include <EspException.hpp>
#include <I2CManager.hpp>
#include <Named.hpp>
#include <PcntManager.hpp>
#include <PulseCounter.hpp>
#include <PwmManager.hpp>
#include <Telemetry.hpp>
#include <drivers/SwitchManager.hpp>
#include <utility>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

namespace farmhub::peripherals {

// Peripherals

class PeripheralBase
    : public Named {
public:
    explicit PeripheralBase(const std::string& name)
        : Named(name) {
    }

    virtual ~PeripheralBase() = default;

    struct ShutdownParameters {
        // Placeholder for future parameters
    };

    virtual void shutdown(const ShutdownParameters parameters) {
    }
};

template <std::derived_from<ConfigurationSection> TConfig>
class Peripheral
    : public PeripheralBase {
public:
    explicit Peripheral(const std::string& name)
        : PeripheralBase(name) {
    }

    virtual void configure(std::shared_ptr<TConfig> /*config*/) = 0;
};

class SimplePeripheral
    : public Peripheral<EmptyConfiguration> {
public:
    explicit SimplePeripheral(const std::string& name, const std::shared_ptr<void>& component)
        : Peripheral<EmptyConfiguration>(name)
        , component(component) {
    }

    void configure(const std::shared_ptr<EmptyConfiguration> /*config*/) override {
        LOGV("No configuration to apply for simple peripheral: %s", name.c_str());
    }

protected:
    const std::shared_ptr<void> component;
};

// Peripheral factories

class PeripheralCreationException
    : public std::runtime_error {
public:
    explicit PeripheralCreationException(const std::string& reason)
        : std::runtime_error(reason) {
    }
};

struct PeripheralServices {
    const std::shared_ptr<I2CManager> i2c;
    const std::shared_ptr<PcntManager> pcntManager;
    const std::shared_ptr<PulseCounterManager> pulseCounterManager;
    const std::shared_ptr<PwmManager> pwmManager;
    const std::shared_ptr<SwitchManager> switches;
    const std::shared_ptr<TelemetryPublisher>& telemetryPublisher;
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

class PeripheralFactoryBase {
public:
    PeripheralFactoryBase(const std::string& factoryType, const std::string& peripheralType)
        : factoryType(factoryType)
        , peripheralType(peripheralType) {
    }

    virtual ~PeripheralFactoryBase() = default;

    virtual std::shared_ptr<PeripheralBase> createPeripheral(PeripheralInitParameters& params, const std::shared_ptr<FileSystem>& fs, const std::string& jsonSettings, JsonObject& initConfigJson) = 0;

    const std::string factoryType;
    const std::string peripheralType;
};

template <std::derived_from<ConfigurationSection> TSettings, std::derived_from<ConfigurationSection> TConfig = EmptyConfiguration, typename... TSettingsArgs>
class PeripheralFactory : public PeripheralFactoryBase {
public:
    // By default use the factory type as the peripheral type
    explicit PeripheralFactory(const std::string& type, TSettingsArgs... settingsArgs)
        : PeripheralFactory(type, type, std::forward<TSettingsArgs>(settingsArgs)...) {
    }

    PeripheralFactory(const std::string& factoryType, const std::string& peripheralType, TSettingsArgs... settingsArgs)
        : PeripheralFactoryBase(factoryType, peripheralType)
        , settingsArgs(std::forward<TSettingsArgs>(settingsArgs)...) {
    }

    std::shared_ptr<PeripheralBase> createPeripheral(PeripheralInitParameters& params, const std::shared_ptr<FileSystem>& fs, const std::string& jsonSettings, JsonObject& initConfigJson) override {
        std::shared_ptr<TSettings> settings = std::apply([](TSettingsArgs... args) {
            return std::make_shared<TSettings>(std::forward<TSettingsArgs>(args)...);
        },
            settingsArgs);
        settings->loadFromString(jsonSettings);
        const auto& name = params.name;
        std::shared_ptr<Peripheral<TConfig>> peripheral = createPeripheral(params, settings);

        std::shared_ptr<TConfig> config = std::make_shared<TConfig>();
        // Use short prefix because SPIFFS has a 32 character limit
        std::shared_ptr<ConfigurationFile<TConfig>> configFile = std::make_shared<ConfigurationFile<TConfig>>(fs, "/p/" + name, config);
        peripheral->configure(config);
        // Store configuration in init message
        config->store(initConfigJson, false);

        params.mqttRoot->subscribe("config", [name, configFile, peripheral](const std::string&, const JsonObject& configJson) {
            LOGD("Received configuration update for peripheral: %s", name.c_str());
            try {
                configFile->update(configJson);
                peripheral->configure(configFile->getConfig());
            } catch (const std::exception& e) {
                LOGE("Failed to update configuration for peripheral '%s' because %s",
                    name.c_str(), e.what());
            }
        });
        return peripheral;
    }

    virtual std::shared_ptr<Peripheral<TConfig>> createPeripheral(PeripheralInitParameters& params, const std::shared_ptr<TSettings>& settings) = 0;

private:
    std::tuple<TSettingsArgs...> settingsArgs;
};

// Peripheral manager

class PeripheralManager final {
public:
    PeripheralManager(
        std::shared_ptr<FileSystem> fs,
        std::shared_ptr<TelemetryCollector> telemetryCollector,
        PeripheralServices services,
        const std::shared_ptr<MqttRoot>& mqttDeviceRoot)
        : fs(std::move(fs))
        , telemetryCollector(std::move(telemetryCollector))
        , services(std::move(services))
        , mqttDeviceRoot(mqttDeviceRoot) {
    }

    void registerFactory(std::unique_ptr<PeripheralFactoryBase> factory) {
        LOGD("Registering peripheral factory: %s",
            factory->factoryType.c_str());
        factories.insert(std::make_pair(factory->factoryType, std::move(factory)));
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

        std::string name = settings->name.get();
        std::string type = settings->type.get();
        auto initJson = peripheralsInitJson.add<JsonObject>();
        initJson["name"] = name;
        initJson["factory"] = type;
        try {
            Lock lock(stateMutex);
            if (state == State::Stopped) {
                LOGE("Not creating peripheral '%s' because the peripheral manager is stopped",
                    name.c_str());
                return false;
            }
            JsonDocument initConfigDoc;
            JsonObject initConfigJson = initConfigDoc.to<JsonObject>();

            LOGD("Creating peripheral '%s' with factory '%s'",
                name.c_str(), type.c_str());
            auto it = factories.find(type);
            if (it == factories.end()) {
                throw PeripheralCreationException("Factory not found: '" + type + "'");
            }
            const std::string& peripheralType = it->second->peripheralType;

            initJson["type"] = peripheralType;
            settings->params.store(initJson, true);

            std::shared_ptr<MqttRoot> mqttRoot = mqttDeviceRoot->forSuffix("peripherals/" + peripheralType + "/" + name);
            PeripheralInitParameters params = {
                name,
                mqttRoot,
                services,
                telemetryCollector,
                initJson["features"].to<JsonArray>(),
            };
            std::shared_ptr<PeripheralBase> peripheral = it->second->createPeripheral(params, fs, settings->params.get().get(), initConfigJson);

            initJson["config"].to<JsonObject>().set(initConfigJson);
            peripherals.push_back(std::move(peripheral));

            return true;
        } catch (const std::exception& e) {
            LOGE("Failed to create '%s' peripheral '%s' because %s",
                type.c_str(), name.c_str(), e.what());
            initJson["error"] = std::string(e.what());
            return false;
        } catch (...) {
            LOGE("Failed to create '%s' peripheral '%s' because of an unknown exception",
                type.c_str(), name.c_str());
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
        PeripheralBase::ShutdownParameters parameters;
        for (auto& peripheral : peripherals) {
            LOGI("Shutting down peripheral '%s'",
                peripheral->name.c_str());
            peripheral->shutdown(parameters);
        }
    }

private:
    class PeripheralSettings : public ConfigurationSection {
    public:
        Property<std::string> name { this, "name", "default" };
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
    std::map<std::string, std::unique_ptr<PeripheralFactoryBase>> factories;
    Mutex stateMutex;
    State state = State::Running;
    std::list<std::shared_ptr<PeripheralBase>> peripherals;
};

}    // namespace farmhub::peripherals
