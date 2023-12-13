#pragma once

#include <list>
#include <map>
#include <memory>

#include <kernel/Configuration.hpp>

namespace farmhub { namespace kernel {

class PeripheralConfiguration : public Configuration {
public:
    PeripheralConfiguration()
        : Configuration("peripheral", 1024) {
    }

    Property<String> name { this, "name", "" };
    Property<String> type { this, "type", "" };
    RawJsonEntry params { this, "params" };
};

class PeripheralsConfiguration : public Configuration {
public:
    PeripheralsConfiguration()
        : Configuration("peripherals", 8192) {
    }

    ArrayProperty<JsonObject> peripherals { this, "peripherals" };
};

class Peripheral {
public:
    Peripheral(const String& name)
        : name(name) {
    }

    virtual ~Peripheral() {
    }

    const String name;
};

class PeripheralFactoryBase {
public:
    PeripheralFactoryBase(const String& type)
        : type(type) {
    }

    virtual std::unique_ptr<Peripheral> createPeripheral(const String& name, const JsonObject& jsonConfig) = 0;

    const String type;
};

template <typename TConfig>
class PeripheralFactory : public PeripheralFactoryBase {
public:
    PeripheralFactory(const String& type, size_t capacity = 2048)
        : PeripheralFactoryBase(type)
        , capacity(capacity) {
    }

    virtual std::unique_ptr<TConfig> createConfig(const String& name) = 0;

    std::unique_ptr<Peripheral> createPeripheral(const String& name, const JsonObject& jsonConfig) override {
        std::unique_ptr<TConfig> config = createConfig(name);
        Serial.println("Configuring peripheral: " + name + " of type " + type);
        config->update(jsonConfig);
        return createPeripheral(*config);
    }

    virtual std::unique_ptr<Peripheral> createPeripheral(TConfig& config) = 0;

private:
    const size_t capacity;
};

class PeripheralManager {
public:
    PeripheralManager(PeripheralsConfiguration& config)
        : config(config) {
        config.onUpdate([this](const JsonObject& json) {
            // Do this via a queue to avoid blocking the config update
            this->updateConfig();
        });
        updateConfig();
    }

    void registerFactory(std::unique_ptr<PeripheralFactoryBase> factory) {
        Serial.println("Registering peripheral factory: " + factory->type);
        factories[factory->type] = std::move(factory);
    }

private:
    void updateConfig() {
        // TODO Ensure mutual exclusion
        // Stop all peripherals
        peripherals.clear();

        Serial.println("Loading peripherals configuration with " + String(config.peripherals.get().size()) + " peripherals");

        for (auto perpheralConfigJson : config.peripherals.get()) {
            PeripheralConfiguration perpheralConfig;
            perpheralConfig.update(perpheralConfigJson);
            auto peripheral = createPeripheral(perpheralConfig.name.get(), perpheralConfig.type.get(), perpheralConfig.params.get());
            if (peripheral == nullptr) {
                Serial.println("Failed to create peripheral: " + perpheralConfig.name.get() + " of type " + perpheralConfig.type.get());
                return;
            }
            peripherals.push_back(std::move(peripheral));
        }
    }

    std::unique_ptr<Peripheral> createPeripheral(const String& name, const String& type, const JsonObject& config) {
        Serial.println("Creating peripheral: " + name + " of type " + type);
        auto it = factories.find(type);
        if (it == factories.end()) {
            // TODO Handle the case where no factory is found for the given type
            Serial.println("No factory found for peripheral type: " + type + " among " + String(factories.size()) + " factories");
            return nullptr;
        }
        return it->second->createPeripheral(name, config);
    }

    PeripheralsConfiguration config;
    std::map<String, std::unique_ptr<PeripheralFactoryBase>> factories;
    std::list<std::unique_ptr<Peripheral>> peripherals;
};

}}    // namespace farmhub::kernel
