#pragma once

#include <map>
#include <memory>

#include <ArduinoLog.h>

#include <kernel/Configuration.hpp>
#include <kernel/Telemetry.hpp>

using std::move;
using std::unique_ptr;

using namespace farmhub::kernel;

namespace farmhub { namespace devices {

// Peripherals

class Peripheral {
public:
    Peripheral(const String& name)
        : name(name) {
    }

    virtual TelemetryProvider* getAsTelemetryProvider() {
        return nullptr;
    }

    const String name;
};

class TelemetryProvidingPeripheral : public Peripheral, public TelemetryProvider {
public:
    TelemetryProvidingPeripheral(const String& name)
        : Peripheral(name) {
    }

    // Poor man's RTTI
    TelemetryProvider* getAsTelemetryProvider() override {
        return this;
    }
};

// Peripheral factories

class PeripheralFactoryBase {
public:
    PeripheralFactoryBase(const String& type)
        : type(type) {
    }

    virtual unique_ptr<Peripheral> createPeripheral(const String& name, const String& jsonConfig) = 0;

    const String type;
};

template <typename TConfig>
class PeripheralFactory : public PeripheralFactoryBase {
public:
    PeripheralFactory(const String& type)
        : PeripheralFactoryBase(type) {
    }

    virtual unique_ptr<TConfig> createConfig() = 0;

    unique_ptr<Peripheral> createPeripheral(const String& name, const String& jsonConfig) override {
        unique_ptr<TConfig> config = createConfig();
        Log.traceln("Configuring peripheral: %s of type %s", name.c_str(), type.c_str());
        config->loadFromString(jsonConfig);
        return createPeripheral(name, move(config));
    }

    virtual unique_ptr<Peripheral> createPeripheral(const String& name, unique_ptr<const TConfig> config) = 0;
};

// Peripheral manager

class PeripheralManager
    : public TelemetryProvider {
public:
    class ConstructionConfiguration : public ConfigurationSection {
    public:
        Property<String> name { this, "name" };
        Property<String> type { this, "type" };
        Property<JsonAsString> params { this, "params" };
    };

    PeripheralManager(ObjectArrayProperty<JsonAsString>& peripheralsConfig)
        : peripheralsConfig(peripheralsConfig) {
        // TODO Update config from MQTT
    }

    void registerFactory(PeripheralFactoryBase& factory) {
        Log.traceln("Registering peripheral factory: %s",
            factory.type.c_str());
        factories.insert(std::make_pair(factory.type, std::reference_wrapper<PeripheralFactoryBase>(factory)));
    }

    void begin() {
        // TODO Rebuild peripherals when config changes
        updateConfig();
    }

    void populateTelemetry(JsonObject& json) override {
        for (auto& peripheral : peripherals) {
            TelemetryProvider* telemetryProvider = peripheral.get()->getAsTelemetryProvider();
            if (telemetryProvider != nullptr) {
                JsonObject peripheralJson = json.createNestedObject(peripheral->name);
                telemetryProvider->populateTelemetry(peripheralJson);
            }
        }
    }

private:
    void updateConfig() {
        // TODO Properly stop all peripherals
        peripherals.clear();

        Log.infoln("Loading configuration for %d peripherals",
            peripheralsConfig.get().size());

        for (auto& perpheralConfigJsonAsString : peripheralsConfig.get()) {
            ConstructionConfiguration perpheralConfig;
            perpheralConfig.loadFromString(perpheralConfigJsonAsString.get());
            unique_ptr<Peripheral> peripheral = createPeripheral(perpheralConfig.name.get(), perpheralConfig.type.get(), perpheralConfig.params.get().get());
            if (peripheral == nullptr) {
                Log.errorln("Failed to create peripheral: %s of type %s",
                    perpheralConfig.name.get().c_str(), perpheralConfig.type.get().c_str());
                return;
            }
            peripherals.push_back(move(peripheral));
        }
    }

    unique_ptr<Peripheral> createPeripheral(const String& name, const String& type, const String& configJson) {
        Log.traceln("Creating peripheral: %s of type %s",
            name.c_str(), type.c_str());
        auto it = factories.find(type);
        if (it == factories.end()) {
            // TODO Handle the case where no factory is found for the given type
            Log.errorln("No factory found for peripheral type: %s among %d factories",
                type.c_str(), factories.size());
            return nullptr;
        }
        // TODO Make this configurable
        return it->second.get().createPeripheral(name, configJson);
    }

    ObjectArrayProperty<JsonAsString>& peripheralsConfig;

    // TODO Use an unordered_map?
    std::map<String, std::reference_wrapper<PeripheralFactoryBase>> factories;
    // TODO Use smart pointers
    std::list<unique_ptr<Peripheral>> peripherals;
};

}}    // namespace farmhub::devices
