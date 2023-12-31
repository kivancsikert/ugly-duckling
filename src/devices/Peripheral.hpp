#pragma once

#include <map>
#include <memory>

#include <ArduinoLog.h>

#include <kernel/Configuration.hpp>
#include <kernel/Named.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/drivers/MqttDriver.hpp>

using std::move;
using std::unique_ptr;

using namespace farmhub::kernel;

namespace farmhub { namespace devices {

// Peripherals

class PeripheralBase
    : public TelemetryProvider
    , public Named {
public:
    PeripheralBase(const String& name, MqttDriver::MqttRoot& mqttRoot, size_t telemetrySize = 2048)
        : Named(name)
        , mqttRoot(mqttRoot)
        , telemetrySize(telemetrySize) {
    }

    virtual ~PeripheralBase() = default;

    void publishTelemetry() {
        DynamicJsonDocument telemetryDoc(telemetrySize);
        JsonObject telemetryJson = telemetryDoc.to<JsonObject>();
        populateTelemetry(telemetryJson);
        if (telemetryJson.begin() == telemetryJson.end()) {
            // No telemetry added
            return;
        }
        // TODO Add device ID
        mqttRoot.publish("telemetry", telemetryDoc);
    }

    virtual void populateTelemetry(JsonObject& telemetryJson) {
    }

protected:
    MqttDriver::MqttRoot mqttRoot;

private:
    const size_t telemetrySize;
};

template <typename TConfig>
class Peripheral
    : public PeripheralBase {
public:
    Peripheral(const String& name, MqttDriver::MqttRoot& mqttRoot)
        : PeripheralBase(name, mqttRoot) {
    }

    virtual void configure(const TConfig& config) {
        Log.verboseln("No configuration to apply for peripheral: %s", name.c_str());
    }
};

// Peripheral factories

class PeripheralCreationException
    : public std::exception {
public:
    PeripheralCreationException(const String& name, const String& reason)
        : name(name)
        , reason(reason) {
    }

    const char* what() const noexcept override {
        return String("Failed to create peripheral '" + name + "' because " + reason).c_str();
    }

    const String name;
    const String reason;
};

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
        ConfigurationFile<TConfig>* configFile = new ConfigurationFile<TConfig>(FileSystem::get(), "/peripherals/" + name + ".json");
        mqttRoot.subscribe("config", [name, configFile](const String&, const JsonObject& configJson) {
            Log.traceln("Received configuration update for peripheral: %s", name.c_str());
            configFile->update(configJson);
        });

        // TODO Use smart pointers
        TDeviceConfig* deviceConfig = createDeviceConfig();
        deviceConfig->loadFromString(jsonConfig);
        Peripheral<TConfig>* peripheral = createPeripheral(name, *deviceConfig, mqttRoot);
        peripheral->configure(configFile->config);
        return peripheral;
    }

    virtual Peripheral<TConfig>* createPeripheral(const String& name, const TDeviceConfig& deviceConfig, MqttDriver::MqttRoot mqttRoot) = 0;
};

// Peripheral manager

class PeripheralManager
    : public TelemetryPublisher {
public:
    PeripheralManager(MqttDriver& mqtt, ArrayProperty<JsonAsString>& peripheralsConfig)
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
            try {
                PeripheralBase* peripheral = createPeripheral(name, type, deviceConfig.params.get().get());
                peripherals.push_back(unique_ptr<PeripheralBase>(peripheral));
            } catch (const PeripheralCreationException& e) {
                Log.errorln("Failed to create peripheral: %s of type %s because %s",
                    name.c_str(), type.c_str(), e.reason.c_str());
            }
        }
    }

    void publishTelemetry() override {
        for (auto& peripheral : peripherals) {
            peripheral->publishTelemetry();
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
            throw PeripheralCreationException(name, "No factory found for peripheral type '" + type + "'");
        }
        MqttDriver::MqttRoot mqttRoot(mqtt, "peripherals/" + type + "/" + name);
        PeripheralFactoryBase& factory = it->second.get();
        PeripheralBase* peripheral = factory.createPeripheral(name, configJson, mqttRoot);
        return peripheral;
    }

    MqttDriver& mqtt;
    ArrayProperty<JsonAsString>& peripheralsConfig;

    // TODO Use an unordered_map?
    std::map<String, std::reference_wrapper<PeripheralFactoryBase>> factories;
    std::list<unique_ptr<PeripheralBase>> peripherals;
};

}}    // namespace farmhub::devices
