#pragma once

#include <map>
#include <memory>

#include <BootClock.hpp>
#include <Configuration.hpp>
#include <EspException.hpp>
#include <I2CManager.hpp>
#include <Named.hpp>
#include <PcntManager.hpp>
#include <PulseCounter.hpp>
#include <PwmManager.hpp>
#include <Telemetry.hpp>
#include <drivers/SwitchManager.hpp>
#include <mqtt/MqttRoot.hpp>
#include <utility>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::kernel::mqtt;

namespace farmhub::peripherals {

// Peripherals

class PeripheralBase
    : public TelemetryProvider,
      public Named {
public:
    PeripheralBase(const std::string& name, const std::shared_ptr<MqttRoot>& mqttRoot)
        : Named(name)
        , mqttRoot(mqttRoot) {
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
    std::shared_ptr<MqttRoot> mqttRoot;
};

template <std::derived_from<ConfigurationSection> TConfig>
class Peripheral
    : public PeripheralBase {
public:
    Peripheral(const std::string& name, std::shared_ptr<MqttRoot> mqttRoot)
        : PeripheralBase(name, mqttRoot) {
    }

    virtual void configure(const std::shared_ptr<TConfig> config) {
        LOGV("No configuration to apply for peripheral: %s", name.c_str());
    }
};

// Peripheral factories

class PeripheralCreationException
    : public std::runtime_error {
public:
    explicit PeripheralCreationException(const std::string& reason)
        : std::runtime_error(reason) {
    }
};

struct PeripheralServices {
    const std::shared_ptr<I2CManager> i2c;
    const std::shared_ptr<PcntManager> pcntManager;
    const std::shared_ptr<PulseCounterManager> pulseCounterManager;
    const std::shared_ptr<PwmManager> pwmManager;
    const std::shared_ptr<SwitchManager> switches;
};

class PeripheralFactoryBase {
public:
    PeripheralFactoryBase(const std::string& factoryType, const std::string& peripheralType)
        : factoryType(factoryType)
        , peripheralType(peripheralType) {
    }

    virtual ~PeripheralFactoryBase() = default;

    virtual std::unique_ptr<PeripheralBase> createPeripheral(const std::string& name, const std::string& jsonConfig, std::shared_ptr<MqttRoot> mqttRoot, std::shared_ptr<FileSystem> fs, const PeripheralServices& services, JsonObject& initConfigJson) = 0;

    const std::string factoryType;
    const std::string peripheralType;
};

template <std::derived_from<ConfigurationSection> TDeviceConfig, std::derived_from<ConfigurationSection> TConfig, typename... TDeviceConfigArgs>
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

    std::unique_ptr<PeripheralBase> createPeripheral(const std::string& name, const std::string& jsonConfig, std::shared_ptr<MqttRoot> mqttRoot, std::shared_ptr<FileSystem> fs, const PeripheralServices& services, JsonObject& initConfigJson) override {
        std::shared_ptr<TConfig> config = std::make_shared<TConfig>();
        // Use short prefix because SPIFFS has a 32 character limit
        std::shared_ptr<ConfigurationFile<TConfig>> configFile = std::make_shared<ConfigurationFile<TConfig>>(fs, "/p/" + name, config);
        mqttRoot->subscribe("config", [name, configFile](const std::string&, const JsonObject& configJson) {
            LOGD("Received configuration update for peripheral: %s", name.c_str());
            try {
                configFile->update(configJson);
            } catch (const std::exception& e) {
                LOGE("Failed to update configuration for peripheral '%s' because %s",
                    name.c_str(), e.what());
            }
        });

        std::shared_ptr<TDeviceConfig> deviceConfig = std::apply([](TDeviceConfigArgs... args) {
            return std::make_shared<TDeviceConfig>(std::forward<TDeviceConfigArgs>(args)...);
        },
            deviceConfigArgs);
        deviceConfig->loadFromString(jsonConfig);
        std::unique_ptr<Peripheral<TConfig>> peripheral = createPeripheral(name, deviceConfig, mqttRoot, services);
        peripheral->configure(config);

        // Store configuration in init message
        config->store(initConfigJson, false);
        return peripheral;
    }

    virtual std::unique_ptr<Peripheral<TConfig>> createPeripheral(const std::string& name, const std::shared_ptr<TDeviceConfig> deviceConfig, std::shared_ptr<MqttRoot> mqttRoot, const PeripheralServices& services) = 0;

private:
    std::tuple<TDeviceConfigArgs...> deviceConfigArgs;
};

// Peripheral manager

class PeripheralManager final
    : public TelemetryPublisher {
public:
    PeripheralManager(
        std::shared_ptr<FileSystem> fs,
        PeripheralServices services,
        const std::shared_ptr<MqttRoot>& mqttDeviceRoot)
        : fs(std::move(fs))
        , services(std::move(services))
        , mqttDeviceRoot(mqttDeviceRoot) {
    }

    void registerFactory(std::unique_ptr<PeripheralFactoryBase> factory) {
        LOGD("Registering peripheral factory: %s",
            factory->factoryType.c_str());
        factories.insert(std::make_pair(factory->factoryType, std::move(factory)));
    }

    bool createPeripheral(const std::string& peripheralConfig, JsonArray peripheralsInitJson) {
        LOGI("Creating peripheral with config: %s",
            peripheralConfig.c_str());
        std::shared_ptr<PeripheralDeviceConfiguration> deviceConfig = std::make_shared<PeripheralDeviceConfiguration>();
        try {
            deviceConfig->loadFromString(peripheralConfig);
        } catch (const std::exception& e) {
            LOGE("Failed to parse peripheral config because %s:\n%s",
                e.what(), peripheralConfig.c_str());
            return false;
        }

        std::string name = deviceConfig->name.get();
        std::string type = deviceConfig->type.get();
        JsonObject initJson = peripheralsInitJson.add<JsonObject>();
        deviceConfig->store(initJson, true);
        try {
            Lock lock(stateMutex);
            if (state == State::Stopped) {
                LOGE("Not creating peripheral '%s' because the peripheral manager is stopped",
                    name.c_str());
                return false;
            }
            JsonDocument initConfigDoc;
            JsonObject initConfigJson = initConfigDoc.to<JsonObject>();
            std::unique_ptr<PeripheralBase> peripheral = createPeripheral(name, type, deviceConfig->params.get().get(), initConfigJson);
            initJson["config"].to<JsonObject>().set(initConfigJson);
            peripherals.push_back(std::move(peripheral));

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

    std::unique_ptr<PeripheralBase> createPeripheral(const std::string& name, const std::string& factoryType, const std::string& configJson, JsonObject& initConfigJson) {
        LOGD("Creating peripheral '%s' with factory '%s'",
            name.c_str(), factoryType.c_str());
        auto it = factories.find(factoryType);
        if (it == factories.end()) {
            throw PeripheralCreationException("Factory not found: '" + factoryType + "'");
        }
        const std::string& peripheralType = it->second.get()->peripheralType;
        std::shared_ptr<MqttRoot> mqttRoot = mqttDeviceRoot->forSuffix("peripherals/" + peripheralType + "/" + name);
        return it->second.get()->createPeripheral(name, configJson, mqttRoot, fs, services, initConfigJson);
    }

    enum class State : uint8_t {
        Running,
        Stopped
    };

    const std::shared_ptr<FileSystem> fs;
    const PeripheralServices services;
    const std::shared_ptr<MqttRoot> mqttDeviceRoot;

    // TODO Use an unordered_map?
    std::map<std::string, std::unique_ptr<PeripheralFactoryBase>> factories;
    Mutex stateMutex;
    State state = State::Running;
    std::list<std::unique_ptr<PeripheralBase>> peripherals;
};

}    // namespace farmhub::peripherals
