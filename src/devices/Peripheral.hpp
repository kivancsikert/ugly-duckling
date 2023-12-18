#pragma once

#include <map>

#include <kernel/Configuration.hpp>

using namespace farmhub::kernel;

namespace farmhub { namespace devices {

// Configuration

class PeripheralConfiguration : public ConfigurationSection {
public:
    PeripheralConfiguration() {
    }

    Property<String> name { this, "name" };
    Property<String> type { this, "type" };
    Property<JsonAsString> params { this, "params" };
};

class PeripheralsConfiguration : public ConfigurationSection {
public:
    PeripheralsConfiguration() {
    }

    ObjectArrayProperty<JsonAsString> peripherals { this, "peripherals" };
};

// Peripherals

class Peripheral {
public:
    Peripheral(const String& name)
        : name(name) {
    }

    const String name;
};

// Peripheral factories

class PeripheralFactoryBase {
public:
    PeripheralFactoryBase(const String& type)
        : type(type) {
    }

    virtual Peripheral* createPeripheral(const String& name, const JsonObject& jsonConfig) = 0;

    const String type;
};

template <typename TConfig>
class PeripheralFactory : public PeripheralFactoryBase {
public:
    PeripheralFactory(const String& type)
        : PeripheralFactoryBase(type) {
    }

    Peripheral* createPeripheral(const String& name, const JsonObject& jsonConfig) override {
        Serial.println("Creating peripheral: " + name + " of type " + type);
        serializeJson(jsonConfig, Serial);

        TConfig config(name);
        Serial.println("Configuring peripheral: " + name + " of type " + type);
        config.update(jsonConfig);
        return createPeripheral(config);
    }

    virtual Peripheral* createPeripheral(TConfig config) = 0;
};

// Peripheral manager

class PeripheralManager {
public:
    PeripheralManager() {
        // TODO Update config from MQTT
    }

    void registerFactory(PeripheralFactoryBase& factory) {
        Serial.println("Registering peripheral factory: " + factory.type);
        factories.insert(std::make_pair(factory.type, std::reference_wrapper<PeripheralFactoryBase>(factory)));
    }

    void begin() {
        // TODO Rebuild peripherals when config changes
        updateConfig();
    }

private:
    void updateConfig() {
        // TODO Properly stop all peripherals
        peripherals.clear();

        Serial.println("Loading peripherals configuration with " + String(config.peripherals.get().size()) + " peripherals");

        for (auto& perpheralConfigJsonAsString : config.peripherals.get()) {
            PeripheralConfiguration perpheralConfig;
            perpheralConfig.loadFromString(perpheralConfigJsonAsString.get());
            auto peripheral = createPeripheral(perpheralConfig.name.get(), perpheralConfig.type.get(), perpheralConfig.params.get().get());
            if (peripheral == nullptr) {
                Serial.println("Failed to create peripheral: " + perpheralConfig.name.get() + " of type " + perpheralConfig.type.get());
                return;
            }
            peripherals.push_back(*peripheral);
        }
    }

    Peripheral* createPeripheral(const String& name, const String& type, const String& configJson) {
        Serial.println("Creating peripheral: " + name + " of type " + type);
        auto it = factories.find(type);
        if (it == factories.end()) {
            // TODO Handle the case where no factory is found for the given type
            Serial.println("No factory found for peripheral type: " + type + " among " + String(factories.size()) + " factories");
            return nullptr;
        }
        // TODO Make this configurable
        DynamicJsonDocument config(2048);
        deserializeJson(config, configJson);
        if (config.isNull()) {
            Serial.println("Failed to parse peripheral configuration: " + configJson);
            return nullptr;
        }
        return it->second.get().createPeripheral(name, config.as<JsonObject>());
    }

    ConfigurationFile<PeripheralsConfiguration> configFile { FileSystem::get(), "/peripherals.json" };
    PeripheralsConfiguration config { configFile.config };

    // TODO Use an unordered_map?
    std::map<String, std::reference_wrapper<PeripheralFactoryBase>> factories;
    // TODO Use smart pointers
    std::list<std::reference_wrapper<Peripheral>> peripherals;
};

}}    // namespace farmhub::devices
