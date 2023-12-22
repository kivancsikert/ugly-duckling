#pragma once

#include <map>
#include <memory>

#include <ArduinoLog.h>

#include <kernel/Configuration.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/drivers/MqttDriver.hpp>

using std::move;
using std::unique_ptr;

using namespace farmhub::kernel;

namespace farmhub { namespace devices {

// Peripherals

class PeripheralBase
    : public TelemetryProvider {
public:
    PeripheralBase(const String& name)
        : name(name) {
    }

    virtual ~PeripheralBase() = default;

    const String name;

protected:
    virtual void updateConfiguration(const JsonObject& json) = 0;
    friend class PeripheralManager;
};

template <typename TConfig>
class Peripheral
    : public PeripheralBase {
public:
    Peripheral(const String& name)
        : PeripheralBase(name) {
        configFile.onUpdate([this, name](const JsonObject& json) {
            Log.traceln("Config file updated for peripheral: %s", name.c_str());
            configure(configFile.config);
        });
        configure(configFile.config);
    }

    virtual void populateTelemetry(JsonObject& json) {
    }

protected:
    virtual void configure(const TConfig& config) {
    }

    void updateConfiguration(const JsonObject& json) {
        configFile.update(json);
    };

private:
    ConfigurationFile<TConfig> configFile { FileSystem::get(), "/peripherals/" + name + ".json" };
    friend class PeripheralManager;
};

// Peripheral factories

class PeripheralFactoryBase {
public:
    PeripheralFactoryBase(const String& type)
        : type(type) {
    }

    virtual PeripheralBase* createPeripheral(const String& name, const String& jsonConfig) = 0;

    const String type;
};

template <typename TConstructionConfig>
class PeripheralFactory : public PeripheralFactoryBase {
public:
    PeripheralFactory(const String& type)
        : PeripheralFactoryBase(type) {
    }

    virtual TConstructionConfig* createConstructionConfig() = 0;

    PeripheralBase* createPeripheral(const String& name, const String& jsonConfig) override {
        TConstructionConfig* config = createConstructionConfig();
        Log.traceln("Creating peripheral: %s of type %s", name.c_str(), type.c_str());
        config->loadFromString(jsonConfig);
        return createPeripheral(name, *config);
    }

    virtual PeripheralBase* createPeripheral(const String& name, const TConstructionConfig& config) = 0;
};

// Peripheral manager

class PeripheralManager
    : public TelemetryProvider {
public:
    PeripheralManager(MqttDriver& mqtt, ObjectArrayProperty<JsonAsString>& peripheralsConfig)
        : mqtt(mqtt)
        , peripheralsConfig(peripheralsConfig) {
    }

    void registerFactory(PeripheralFactoryBase& factory) {
        Log.traceln("Registering peripheral factory: %s",
            factory.type.c_str());
        factories.insert(std::make_pair(factory.type, std::reference_wrapper<PeripheralFactoryBase>(factory)));
    }

    void begin() {
        Log.infoln("Loading configuration for %d peripherals",
            peripheralsConfig.get().size());

        for (auto& perpheralConfigJsonAsString : peripheralsConfig.get()) {
            ConstructionConfiguration constructionConfig;
            constructionConfig.loadFromString(perpheralConfigJsonAsString.get());
            const String& name = constructionConfig.name.get();
            const String& type = constructionConfig.type.get();
            PeripheralBase* peripheral = createPeripheral(name, type, constructionConfig.params.get().get());
            if (peripheral == nullptr) {
                Log.errorln("Failed to create peripheral: %s of type %s",
                    name.c_str(), type.c_str());
                return;
            }
            peripherals.push_back(unique_ptr<PeripheralBase>(peripheral));
        }
    }

    void populateTelemetry(JsonObject& json) override {
        for (auto& peripheral : peripherals) {
            JsonObject peripheralJson = json.createNestedObject(peripheral->name);
            peripheral->populateTelemetry(peripheralJson);
        }
    }

private:
    class ConstructionConfiguration : public ConfigurationSection {
    public:
        Property<String> name { this, "name" };
        Property<String> type { this, "type" };
        Property<JsonAsString> params { this, "params" };
    };

    PeripheralBase* createPeripheral(const String& name, const String& type, const String& configJson) {
        Log.traceln("Creating peripheral: %s of type %s",
            name.c_str(), type.c_str());
        auto it = factories.find(type);
        if (it == factories.end()) {
            // TODO Handle the case where no factory is found for the given type
            Log.errorln("No factory found for peripheral type: %s among %d factories",
                type.c_str(), factories.size());
            return nullptr;
        }
        PeripheralBase* peripheral = it->second.get().createPeripheral(name, configJson);
        MqttDriver::MqttRoot mqttRoot(mqtt, "peripherals/" + type + "/" + name);
        mqttRoot.subscribe("config", [name, peripheral](const String&, const JsonObject& configJson) {
            Log.traceln("Updating configuration for peripheral: %s",
                name.c_str());
            peripheral->updateConfiguration(configJson);
        });
        return peripheral;
    }

    MqttDriver& mqtt;
    ObjectArrayProperty<JsonAsString>& peripheralsConfig;

    // TODO Use an unordered_map?
    std::map<String, std::reference_wrapper<PeripheralFactoryBase>> factories;
    std::list<unique_ptr<PeripheralBase>> peripherals;
};

}}    // namespace farmhub::devices
