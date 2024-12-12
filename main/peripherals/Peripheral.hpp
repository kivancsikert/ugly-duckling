#pragma once

#include <map>
#include <memory>

#include <kernel/BootClock.hpp>
#include <kernel/Configuration.hpp>
#include <kernel/I2CManager.hpp>
#include <kernel/Named.hpp>
#include <kernel/PcntManager.hpp>
#include <kernel/PwmManager.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/drivers/SwitchManager.hpp>
#include <kernel/mqtt/MqttRoot.hpp>

using std::move;
using std::shared_ptr;
using std::unique_ptr;

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::kernel::mqtt;

namespace farmhub::peripherals {

// Peripherals

class PeripheralBase
    : public TelemetryProvider,
      public Named {
public:
    PeripheralBase(const std::string& name, shared_ptr<MqttRoot> mqttRoot, size_t telemetrySize = 2048)
        : Named(name)
        , mqttRoot(mqttRoot)
        , telemetrySize(telemetrySize) {
        mqttRoot->registerCommand("ping", [this](const JsonObject& request, JsonObject& response) {
            LOGV("Received ping request");
            publishTelemetry();
            response["pong"] = duration_cast<milliseconds>(boot_clock::now().time_since_epoch()).count();
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
        mqttRoot->publish("telemetry", telemetryDoc, Retention::NoRetain, QoS::AtLeastOnce);
    }

    virtual void populateTelemetry(JsonObject& telemetryJson) override {
    }

    struct ShutdownParameters {
        // Placeholder for future parameters
    };

    virtual void shutdown(const ShutdownParameters parameters) {
    }

protected:
    shared_ptr<MqttRoot> mqttRoot;

private:
    const size_t telemetrySize;
};

template <typename TConfig>
class Peripheral
    : public PeripheralBase {
public:
    Peripheral(const std::string& name, shared_ptr<MqttRoot> mqttRoot)
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
    PeripheralCreationException(const std::string& reason)
        : message(std::string(reason)) {
    }

    const char* what() const noexcept override {
        return message.c_str();
    }

    const std::string message;
};

struct PeripheralServices {
    I2CManager& i2c;
    PcntManager& pcntManager;
    PwmManager& pwmManager;
    SwitchManager& switches;
};

class PeripheralFactoryBase {
public:
    PeripheralFactoryBase(const std::string& factoryType, const std::string& peripheralType)
        : factoryType(factoryType)
        , peripheralType(peripheralType) {
    }

    virtual unique_ptr<PeripheralBase> createPeripheral(const std::string& name, const std::string& jsonConfig, shared_ptr<MqttRoot> mqttRoot, PeripheralServices& services, JsonObject& initConfigJson) = 0;

    const std::string factoryType;
    const std::string peripheralType;
};

template <typename TDeviceConfig, typename TConfig, typename... TDeviceConfigArgs>
class PeripheralFactory : public PeripheralFactoryBase {
public:
    // By default use the factory type as the peripheral type
    PeripheralFactory(const std::string& type, TDeviceConfigArgs... deviceConfigArgs)
        : PeripheralFactory(type, type, std::forward<TDeviceConfigArgs>(deviceConfigArgs)...) {
    }

    PeripheralFactory(const std::string& factoryType, const std::string& peripheralType, TDeviceConfigArgs... deviceConfigArgs)
        : PeripheralFactoryBase(factoryType, peripheralType)
        , deviceConfigArgs(std::forward<TDeviceConfigArgs>(deviceConfigArgs)...) {
    }

    unique_ptr<PeripheralBase> createPeripheral(const std::string& name, const std::string& jsonConfig, shared_ptr<MqttRoot> mqttRoot, PeripheralServices& services, JsonObject& initConfigJson) override {
        // Use short prefix because SPIFFS has a 32 character limit
        ConfigurationFile<TConfig>* configFile = new ConfigurationFile<TConfig>(FileSystem::get(), "/p/" + name);
        mqttRoot->subscribe("config", [name, configFile](const std::string&, const JsonObject& configJson) {
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

    virtual unique_ptr<Peripheral<TConfig>> createPeripheral(const std::string& name, const TDeviceConfig& deviceConfig, shared_ptr<MqttRoot> mqttRoot, PeripheralServices& services) = 0;

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
        SwitchManager& switchManager,
        const shared_ptr<MqttRoot> mqttDeviceRoot)
        : services({ i2c, pcntManager, pwmManager, switchManager })
        , mqttDeviceRoot(mqttDeviceRoot) {
    }

    void registerFactory(PeripheralFactoryBase& factory) {
        LOGD("Registering peripheral factory: %s",
            factory.factoryType.c_str());
        factories.insert(std::make_pair(factory.factoryType, std::reference_wrapper<PeripheralFactoryBase>(factory)));
    }

    bool createPeripheral(const std::string& peripheralConfig, JsonArray peripheralsInitJson) {
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

        std::string name = deviceConfig.name.get();
        std::string type = deviceConfig.type.get();
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
            initJson["error"] = std::string(e.what());
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
            LOGI("Shutting down peripheral '%s'",
                peripheral->name.c_str());
            peripheral->shutdown(parameters);
        }
    }

private:
    class PeripheralDeviceConfiguration : public ConfigurationSection {
    public:
        Property<std::string> name { this, "name", "default" };
        Property<std::string> type { this, "type" };
        Property<JsonAsString> params { this, "params" };
    };

    unique_ptr<PeripheralBase> createPeripheral(const std::string& name, const std::string& factoryType, const std::string& configJson, JsonObject& initConfigJson) {
        LOGD("Creating peripheral '%s' with factory '%s'",
            name.c_str(), factoryType.c_str());
        auto it = factories.find(factoryType);
        if (it == factories.end()) {
            throw PeripheralCreationException("Factory not found: '" + factoryType + "'");
        }
        const std::string& peripheralType = it->second.get().peripheralType;
        shared_ptr<MqttRoot> mqttRoot = mqttDeviceRoot->forSuffix("peripherals/" + peripheralType + "/" + name);
        PeripheralFactoryBase& factory = it->second.get();
        return factory.createPeripheral(name, configJson, mqttRoot, services, initConfigJson);
    }

    enum class State {
        Running,
        Stopped
    };

    // TODO Make this immutable somehow
    PeripheralServices services;

    const shared_ptr<MqttRoot> mqttDeviceRoot;

    // TODO Use an unordered_map?
    std::map<std::string, std::reference_wrapper<PeripheralFactoryBase>> factories;
    Mutex stateMutex;
    State state = State::Running;
    std::list<unique_ptr<PeripheralBase>> peripherals;
};

}    // namespace farmhub::peripherals
