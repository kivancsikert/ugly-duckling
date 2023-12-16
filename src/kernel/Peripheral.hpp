#pragma once

#include <list>
#include <map>
#include <memory>

#include <kernel/Concurrent.hpp>
#include <kernel/Configuration.hpp>
#include <kernel/Util.hpp>

namespace farmhub { namespace kernel {

class PeripheralConfiguration : public Configuration {
public:
    PeripheralConfiguration()
        : Configuration("peripheral", 1024) {
    }

    Property<String> name { this, "name" };
    Property<String> type { this, "type" };
    Property<JsonAsString> params { this, "params" };
};

class PeripheralsConfiguration : public Configuration {
public:
    PeripheralsConfiguration()
        : Configuration("peripherals", 8192) {
    }

    ObjectArrayProperty<JsonAsString> peripherals { this, "peripherals" };
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
        Serial.println("Creating peripheral: " + name + " of type " + type);
        serializeJson(jsonConfig, Serial);

        std::unique_ptr<TConfig> config = createConfig(name);
        Serial.println("Configuring peripheral: " + name + " of type " + type);
        config->update(jsonConfig);
        return createPeripheral(std::move(config));
    }

    virtual std::unique_ptr<Peripheral> createPeripheral(std::unique_ptr<TConfig> config) = 0;

private:
    const size_t capacity;
};

class PeripheralManager {
public:
    PeripheralManager(PeripheralsConfiguration& config)
        : config(config) {
    }

    void registerFactory(PeripheralFactoryBase& factory) {
        Serial.println("Registering peripheral factory: " + factory.type);
        factories.insert(std::make_pair(factory.type, std::reference_wrapper<PeripheralFactoryBase>(factory)));
    }

    void begin() {
        config.onUpdate([this](const JsonObject& json) {
            // Do this via a queue to avoid blocking the config update
            this->updateConfig();
        });
        updateConfig();
    }

private:
    void updateConfig() {
        configMutex.lock();
        updateConfigUnderMutex();
        configMutex.unlock();
    }

    void updateConfigUnderMutex() {
        // Stop all peripherals
        peripherals.clear();

        Serial.println("Loading peripherals configuration with " + String(config.peripherals.get().size()) + " peripherals");

        for (auto& perpheralConfigJsonAsString : config.peripherals.get()) {
            PeripheralConfiguration perpheralConfig;
            perpheralConfig.load(perpheralConfigJsonAsString.get());
            auto peripheral = createPeripheral(perpheralConfig.name.get(), perpheralConfig.type.get(), perpheralConfig.params.get().get());
            if (peripheral == nullptr) {
                Serial.println("Failed to create peripheral: " + perpheralConfig.name.get() + " of type " + perpheralConfig.type.get());
                return;
            }
            peripherals.push_back(std::move(peripheral));
        }
    }

    std::unique_ptr<Peripheral> createPeripheral(const String& name, const String& type, const String& configJson) {
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

    PeripheralsConfiguration& config;
    std::map<String, std::reference_wrapper<PeripheralFactoryBase>> factories;
    std::list<std::unique_ptr<Peripheral>> peripherals;
    Mutex configMutex;
};

}}    // namespace farmhub::kernel
