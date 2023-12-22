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
};

template <typename TConfig>
class Peripheral
    : public PeripheralBase {
public:
    Peripheral(const String& name)
        : PeripheralBase(name) {
    }

    virtual void populateTelemetry(JsonObject& json) {
    }

    virtual void configure(const TConfig& config) {
        Log.verboseln("No configuration to apply for peripheral: %s", name.c_str());
    }
};

// Peripheral factories

class PeripheralFactoryBase {
public:
    PeripheralFactoryBase(const String& type)
        : type(type) {
    }

    virtual PeripheralBase* createPeripheral(const String& name, const String& jsonConfig, MqttDriver::MqttRoot mqttRoot) = 0;

    const String type;
};

template <typename TDeviceConfig, typename TConfig>
class PeripheralFactory : public PeripheralFactoryBase {
public:
    PeripheralFactory(const String& type)
        : PeripheralFactoryBase(type) {
    }

    virtual TDeviceConfig* createDeviceConfig() = 0;

    PeripheralBase* createPeripheral(const String& name, const String& jsonConfig, MqttDriver::MqttRoot mqttRoot) override {
        Log.traceln("Creating peripheral: %s of type %s", name.c_str(), type.c_str());

        ConfigurationFile<TConfig>* configFile = new ConfigurationFile<TConfig>(FileSystem::get(), "/peripherals/" + name + ".json");
        mqttRoot.subscribe("config", [configFile](const String&, const JsonObject& configJson) {
            configFile->update(configJson);
        });

        TDeviceConfig* deviceConfig = createDeviceConfig();
        deviceConfig->loadFromString(jsonConfig);
        Peripheral<TConfig>* peripheral = createPeripheral(name, *deviceConfig);
        peripheral->configure(configFile->config);
        return peripheral;
    }

    virtual Peripheral<TConfig>* createPeripheral(const String& name, const TDeviceConfig& deviceConfig) = 0;
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
            PeripheralDeviceConfiguration deviceConfig;
            deviceConfig.loadFromString(perpheralConfigJsonAsString.get());
            const String& name = deviceConfig.name.get();
            const String& type = deviceConfig.type.get();
            PeripheralBase* peripheral = createPeripheral(name, type, deviceConfig.params.get().get());
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
    class PeripheralDeviceConfiguration : public ConfigurationSection {
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
        MqttDriver::MqttRoot mqttRoot(mqtt, "peripherals/" + type + "/" + name);
        PeripheralFactoryBase& factory = it->second.get();
        PeripheralBase* peripheral = factory.createPeripheral(name, configJson, mqttRoot);
        return peripheral;
    }

    MqttDriver& mqtt;
    ObjectArrayProperty<JsonAsString>& peripheralsConfig;

    // TODO Use an unordered_map?
    std::map<String, std::reference_wrapper<PeripheralFactoryBase>> factories;
    std::list<unique_ptr<PeripheralBase>> peripherals;
};

}}    // namespace farmhub::devices
