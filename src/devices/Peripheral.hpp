#pragma once

#include <map>
#include <memory>

#include <ArduinoLog.h>

#include <kernel/Configuration.hpp>
#include <kernel/Named.hpp>
#include <kernel/PcntManager.hpp>
#include <kernel/PwmManager.hpp>
#include <kernel/SleepManager.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/drivers/MqttDriver.hpp>

using std::move;
using std::shared_ptr;
using std::unique_ptr;

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

namespace farmhub::devices {

// Peripherals

class PeripheralBase
    : public TelemetryProvider,
      public Named {
public:
    PeripheralBase(const String& name, shared_ptr<MqttDriver::MqttRoot> mqttRoot, size_t telemetrySize = 2048)
        : Named(name)
        , mqttRoot(mqttRoot)
        , telemetrySize(telemetrySize) {
        mqttRoot->registerCommand("ping", [this](const JsonObject& request, JsonObject& response) {
            Serial.println("Received ping request");
            publishTelemetry();
            response["pong"] = millis();
        });
    }

    virtual ~PeripheralBase() = default;

    void publishTelemetry() {
        JsonDocument telemetryDoc;
        JsonObject telemetryJson = telemetryDoc.to<JsonObject>();
        populateTelemetry(telemetryJson);
        if (telemetryJson.begin() == telemetryJson.end()) {
            // No telemetry added
            Log.verboseln("No telemetry to publish for peripheral: %s", name.c_str());
            return;
        }
        mqttRoot->publish("telemetry", telemetryDoc);
    }

    virtual void populateTelemetry(JsonObject& telemetryJson) override {
    }

protected:
    shared_ptr<MqttDriver::MqttRoot> mqttRoot;

private:
    const size_t telemetrySize;
};

template <typename TConfig>
class Peripheral
    : public PeripheralBase {
public:
    Peripheral(const String& name, shared_ptr<MqttDriver::MqttRoot> mqttRoot)
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
        : message(String("PeripheralCreationException: Failed to create peripheral '" + name + "' because " + reason)) {
    }

    const char* what() const noexcept override {
        return message.c_str();
    }

    const String message;
};

struct PeripheralServices {
    PcntManager& pcntManager;
    PwmManager& pwmManager;
    SleepManager& sleepManager;
};

class PeripheralFactoryBase {
public:
    PeripheralFactoryBase(const String& factoryType, const String& peripheralType)
        : factoryType(factoryType)
        , peripheralType(peripheralType) {
    }

    virtual unique_ptr<PeripheralBase> createPeripheral(const String& name, const String& jsonConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot, PeripheralServices& services) = 0;

    const String factoryType;
    const String peripheralType;
};

template <typename TDeviceConfig, typename TConfig, typename... TDeviceConfigArgs>
class PeripheralFactory : public PeripheralFactoryBase {
public:
    // By default use the factory type as the peripheral type
    // TODO Use TDeviceConfigArgs&& instead
    PeripheralFactory(const String& type, TDeviceConfigArgs... deviceConfigArgs)
        : PeripheralFactory(type, type, std::forward<TDeviceConfigArgs>(deviceConfigArgs)...) {
    }

    PeripheralFactory(const String& factoryType, const String& peripheralType, TDeviceConfigArgs... deviceConfigArgs)
        : PeripheralFactoryBase(factoryType, peripheralType)
        , deviceConfigArgs(std::forward<TDeviceConfigArgs>(deviceConfigArgs)...) {
    }

    unique_ptr<PeripheralBase> createPeripheral(const String& name, const String& jsonConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot, PeripheralServices& services) override {
        // Use short prefix because SPIFFS has a 32 character limit
        ConfigurationFile<TConfig>* configFile = new ConfigurationFile<TConfig>(FileSystem::get(), "/p/" + name);
        mqttRoot->subscribe("config", [name, configFile](const String&, const JsonObject& configJson) {
            Log.traceln("Received configuration update for peripheral: %s", name.c_str());
            configFile->update(configJson);
        });

        TDeviceConfig deviceConfig = std::apply([](TDeviceConfigArgs... args) {
            return TDeviceConfig(std::forward<TDeviceConfigArgs>(args)...);
        },
            deviceConfigArgs);
        deviceConfig.loadFromString(jsonConfig);
        unique_ptr<Peripheral<TConfig>> peripheral = createPeripheral(name, deviceConfig, mqttRoot, services);
        peripheral->configure(configFile->config);
        mqttRoot->publish("init", [&](JsonObject& json) {
            auto config = json["config"].to<JsonObject>();
            configFile->config.store(config, false);
        });
        return peripheral;
    }

    virtual unique_ptr<Peripheral<TConfig>> createPeripheral(const String& name, const TDeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot, PeripheralServices& services) = 0;

private:
    std::tuple<TDeviceConfigArgs...> deviceConfigArgs;
};

// Peripheral manager

class PeripheralManager
    : public TelemetryPublisher {
public:
    PeripheralManager(
        PcntManager& pcntManager,
        PwmManager& pwmManager,
        SleepManager& sleepManager,
        const shared_ptr<MqttDriver::MqttRoot> mqttDeviceRoot)
        : services(PeripheralServices { pcntManager, pwmManager, sleepManager })
        , mqttDeviceRoot(mqttDeviceRoot) {
    }

    void registerFactory(PeripheralFactoryBase& factory) {
        Log.traceln("Registering peripheral factory: %s",
            factory.factoryType.c_str());
        factories.insert(std::make_pair(factory.factoryType, std::reference_wrapper<PeripheralFactoryBase>(factory)));
    }

    void createPeripheral(const String& peripheralConfig) {
        Log.infoln("Creating peripheral with config: %s",
            peripheralConfig.c_str());
        PeripheralDeviceConfiguration deviceConfig;
        try {
            deviceConfig.loadFromString(peripheralConfig);
        } catch (const std::exception& e) {
            Log.errorln("Failed to parse peripheral config because %s:\n%s",
                e.what(), peripheralConfig.c_str());
            return;
        }

        const String& name = deviceConfig.name.get();
        const String& factory = deviceConfig.type.get();
        try {
            unique_ptr<PeripheralBase> peripheral = createPeripheral(name, factory, deviceConfig.params.get().get());
            peripherals.push_back(move(peripheral));
        } catch (const std::exception& e) {
            Log.errorln("Failed to create peripheral '%s' with factory '%s' because %s",
                name.c_str(), factory.c_str(), e.what());
        } catch (...) {
            Log.errorln("Failed to create peripheral '%s' with factory '%s' because of an unknown exception",
                name.c_str(), factory.c_str());
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

    unique_ptr<PeripheralBase> createPeripheral(const String& name, const String& factoryType, const String& configJson) {
        Log.traceln("Creating peripheral '%s' with factory '%s'",
            name.c_str(), factoryType.c_str());
        auto it = factories.find(factoryType);
        if (it == factories.end()) {
            throw PeripheralCreationException(name, "Factory not found: '" + factoryType + "'");
        }
        const String& peripheralType = it->second.get().peripheralType;
        shared_ptr<MqttDriver::MqttRoot> mqttRoot = mqttDeviceRoot->forSuffix("peripherals/" + peripheralType + "/" + name);
        PeripheralFactoryBase& factory = it->second.get();
        return factory.createPeripheral(name, configJson, mqttRoot, services);
    }

    // TODO Make this immutable somehow
    PeripheralServices services;

    const shared_ptr<MqttDriver::MqttRoot> mqttDeviceRoot;

    // TODO Use an unordered_map?
    std::map<String, std::reference_wrapper<PeripheralFactoryBase>> factories;
    std::list<unique_ptr<PeripheralBase>> peripherals;
};

}    // namespace farmhub::devices
