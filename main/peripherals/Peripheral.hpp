#pragma once

#include <map>
#include <memory>

#include <kernel/Configuration.hpp>
#include <kernel/I2CManager.hpp>
#include <kernel/Log.hpp>
#include <kernel/Named.hpp>
#include <kernel/PcntManager.hpp>
#include <kernel/PwmManager.hpp>
#include <kernel/SleepManager.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/drivers/MqttDriver.hpp>
#include <kernel/drivers/SwitchManager.hpp>

using std::move;
using std::shared_ptr;
using std::unique_ptr;

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

namespace farmhub::peripherals {

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
            LOGV("Received ping request");
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
            LOGV("No telemetry to publish for peripheral: %s", name.c_str());
            return;
        }
        mqttRoot->publish("telemetry", telemetryDoc);
    }

    virtual void populateTelemetry(JsonObject& telemetryJson) override {
    }

    struct ShutdownParameters {
        // Placeholder for future parameters
    };

    virtual void shutdown(const ShutdownParameters parameters) {
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
        LOGV("No configuration to apply for peripheral: %s", name.c_str());
    }
};

// Peripheral factories

class PeripheralCreationException
    : public std::exception {
public:
    PeripheralCreationException(const String& reason)
        : message(String(reason)) {
    }

    const char* what() const noexcept override {
        return message.c_str();
    }

    const String message;
};

struct PeripheralServices {
    I2CManager& i2c;
    PcntManager& pcntManager;
    PwmManager& pwmManager;
    SleepManager& sleepManager;
    SwitchManager& switches;
};

class PeripheralFactoryBase {
public:
    PeripheralFactoryBase(const String& factoryType, const String& peripheralType)
        : factoryType(factoryType)
        , peripheralType(peripheralType) {
    }

    virtual unique_ptr<PeripheralBase> createPeripheral(const String& name, const String& jsonConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot, PeripheralServices& services, JsonObject& initConfigJson) = 0;

    const String factoryType;
    const String peripheralType;
};

template <typename TDeviceConfig, typename TConfig, typename... TDeviceConfigArgs>
class PeripheralFactory : public PeripheralFactoryBase {
public:
    // By default use the factory type as the peripheral type
    PeripheralFactory(const String& type, TDeviceConfigArgs... deviceConfigArgs)
        : PeripheralFactory(type, type, std::forward<TDeviceConfigArgs>(deviceConfigArgs)...) {
    }

    PeripheralFactory(const String& factoryType, const String& peripheralType, TDeviceConfigArgs... deviceConfigArgs)
        : PeripheralFactoryBase(factoryType, peripheralType)
        , deviceConfigArgs(std::forward<TDeviceConfigArgs>(deviceConfigArgs)...) {
    }

    unique_ptr<PeripheralBase> createPeripheral(const String& name, const String& jsonConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot, PeripheralServices& services, JsonObject& initConfigJson) override {
        // Use short prefix because SPIFFS has a 32 character limit
        ConfigurationFile<TConfig>* configFile = new ConfigurationFile<TConfig>(FileSystem::get(), "/p/" + name);
        mqttRoot->subscribe("config", [name, configFile](const String&, const JsonObject& configJson) {
            LOGD("Received configuration update for peripheral: %s", name.c_str());
            configFile->update(configJson);
        });

        TDeviceConfig deviceConfig = std::apply([](TDeviceConfigArgs... args) {
            return TDeviceConfig(std::forward<TDeviceConfigArgs>(args)...);
        },
            deviceConfigArgs);
        deviceConfig.loadFromString(jsonConfig);
        unique_ptr<Peripheral<TConfig>> peripheral = createPeripheral(name, deviceConfig, mqttRoot, services);
        peripheral->configure(configFile->config);

        // Store configuration in init message
        configFile->config.store(initConfigJson, false);
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
        I2CManager& i2c,
        PcntManager& pcntManager,
        PwmManager& pwmManager,
        SleepManager& sleepManager,
        SwitchManager& switchManager,
        const shared_ptr<MqttDriver::MqttRoot> mqttDeviceRoot)
        : services({ i2c, pcntManager, pwmManager, sleepManager, switchManager })
        , mqttDeviceRoot(mqttDeviceRoot) {
    }

    void registerFactory(PeripheralFactoryBase& factory) {
        LOGD("Registering peripheral factory: %s",
            factory.factoryType.c_str());
        factories.insert(std::make_pair(factory.factoryType, std::reference_wrapper<PeripheralFactoryBase>(factory)));
    }

    bool createPeripheral(const String& peripheralConfig, JsonArray peripheralsInitJson) {
        LOGI("Creating peripheral with config: %s",
            peripheralConfig.c_str());
        PeripheralDeviceConfiguration deviceConfig;
        try {
            deviceConfig.loadFromString(peripheralConfig);
        } catch (const std::exception& e) {
            LOGE("Failed to parse peripheral config because %s:\n%s",
                e.what(), peripheralConfig.c_str());
            return false;
        }

        String name = deviceConfig.name.get();
        String type = deviceConfig.type.get();
        JsonObject initJson = peripheralsInitJson.add<JsonObject>();
        deviceConfig.store(initJson, true);
        try {
            Lock lock(stateMutex);
            if (state == State::Stopped) {
                LOGE("Not creating peripheral '%s' because the peripheral manager is stopped",
                    name.c_str());
                return false;
            }
            JsonDocument initConfigDoc;
            JsonObject initConfigJson = initConfigDoc.to<JsonObject>();
            unique_ptr<PeripheralBase> peripheral = createPeripheral(name, type, deviceConfig.params.get().get(), initConfigJson);
            initJson["config"].to<JsonObject>().set(initConfigJson);
            peripherals.push_back(move(peripheral));

            return true;
        } catch (const std::exception& e) {
            LOGE("Failed to create '%s' peripheral '%s' because %s",
                type.c_str(), name.c_str(), e.what());
            initJson["error"] = String(e.what());
            return false;
        } catch (...) {
            LOGE("Failed to create '%s' peripheral '%s' because of an unknown exception",
                type.c_str(), name.c_str());
            initJson["error"] = "unknown exception";
            return false;
        }
    }

    void publishTelemetry() override {
        Lock lock(stateMutex);
        if (state == State::Stopped) {
            LOGD("Not publishing telemetry because the peripheral manager is stopped");
            return;
        }
        for (auto& peripheral : peripherals) {
            peripheral->publishTelemetry();
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
            Log.printfToSerial("Shutting down peripheral '%s'\n",
                peripheral->name.c_str());
            peripheral->shutdown(parameters);
        }
    }

private:
    class PeripheralDeviceConfiguration : public ConfigurationSection {
    public:
        Property<String> name { this, "name", "default" };
        Property<String> type { this, "type" };
        Property<JsonAsString> params { this, "params" };
    };

    unique_ptr<PeripheralBase> createPeripheral(const String& name, const String& factoryType, const String& configJson, JsonObject& initConfigJson) {
        LOGD("Creating peripheral '%s' with factory '%s'",
            name.c_str(), factoryType.c_str());
        auto it = factories.find(factoryType);
        if (it == factories.end()) {
            throw PeripheralCreationException("Factory not found: '" + factoryType + "'");
        }
        const String& peripheralType = it->second.get().peripheralType;
        shared_ptr<MqttDriver::MqttRoot> mqttRoot = mqttDeviceRoot->forSuffix("peripherals/" + peripheralType + "/" + name);
        PeripheralFactoryBase& factory = it->second.get();
        return factory.createPeripheral(name, configJson, mqttRoot, services, initConfigJson);
    }

    enum class State {
        Running,
        Stopped
    };

    // TODO Make this immutable somehow
    PeripheralServices services;

    const shared_ptr<MqttDriver::MqttRoot> mqttDeviceRoot;

    // TODO Use an unordered_map?
    std::map<String, std::reference_wrapper<PeripheralFactoryBase>> factories;
    Mutex stateMutex;
    State state = State::Running;
    std::list<unique_ptr<PeripheralBase>> peripherals;
};

}    // namespace farmhub::peripherals
