#pragma once

#include <functional>
#include <optional>

#include <freertos/FreeRTOS.h>

#include <kernel/Command.hpp>
#include <kernel/FileSystem.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/drivers/LedDriver.hpp>
#include <kernel/drivers/MdnsDriver.hpp>
#include <kernel/drivers/MqttDriver.hpp>
#include <kernel/drivers/OtaDriver.hpp>
#include <kernel/drivers/RtcDriver.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

#include <version.h>

namespace farmhub { namespace kernel {

using namespace farmhub::kernel::drivers;

template <typename TConfiguration>
class Application;

static RTC_DATA_ATTR int bootCount = 0;

// TODO Move this to a separate file
static const String& getMacAddress() {
    static String macAddress;
    if (macAddress.length() == 0) {
        uint8_t rawMac[6];
        for (int i = 0; i < 6; i++) {
            rawMac[i] = 0;
        }
        if (esp_efuse_mac_get_default(rawMac) != ESP_OK) {
            macAddress = "??:??:??:??:??:??:??:??";
        } else {
            char mac[24];
            sprintf(mac, "%02x:%02x:%02x:%02x:%02x:%02x",
                rawMac[0], rawMac[1], rawMac[2], rawMac[3],
                rawMac[4], rawMac[5]);
            macAddress = mac;
        }
    }
    return macAddress;
}

class DeviceConfiguration : public Configuration {
public:
    DeviceConfiguration(const String& defaultModel, size_t capacity = 2048)
        : Configuration("device", capacity)
        , model(this, "model", defaultModel)
        , instance(this, "instance", getMacAddress()) {
    }

    Property<String> model;
    Property<String> instance;

    MqttDriver::Config mqtt { this, "mqtt" };
    RtcDriver::Config ntp { this, "ntp" };

    virtual bool isResetButtonPressed() {
        return false;
    }

    virtual const String getHostname() {
        String hostname = instance.get();
        hostname.replace(":", "-");
        hostname.replace("?", "");
        return hostname;
    }
};

template <typename TDeviceConfiguration>
class Application {
public:
    Application(LedDriver& statusLed)
        : version(VERSION)
        , fs(FileSystem::get())
        , deviceConfig(Configuration::bindToFile(fs, "/device-config.json", *new TDeviceConfiguration()))
        , statusLed(statusLed) {

        Serial.printf("Initializing version %s on %s instance '%s' with hostname '%s'\n",
            version.c_str(),
            deviceConfig.model.get().c_str(),
            deviceConfig.instance.get().c_str(),
            deviceConfig.getHostname());

        Task::loop("status-update", 4096, [this](Task&) { updateState(); });

        registerCommand(echoCommand);
        // TODO Add ping command
        // registerCommand(pingCommand);
        // TODO Add reset-wifi command
        // registerCommand(resetWifiCommand);
        registerCommand(restartCommand);
        registerCommand(sleepCommand);
        registerCommand(fileListCommand);
        registerCommand(fileReadCommand);
        registerCommand(fileWriteCommand);
        registerCommand(fileRemoveCommand);
        registerCommand(httpUpdateCommand);

        applicationReadyState.awaitSet();

        mqtt.publish(
            "init",
            [&](JsonObject& json) {
                // TODO Remove redundanty mentions of "ugly-duckling"
                json["type"] = "ugly-duckling";
                json["model"] = deviceConfig.model.get();
                json["instance"] = deviceConfig.instance.get();
                json["mac"] = getMacAddress();
                auto device = json.createNestedObject("deviceConfig");
                deviceConfig.store(device, false);
                json["app"] = "ugly-duckling";
                json["version"] = version;
                json["wakeup"] = esp_sleep_get_wakeup_cause();
                json["bootCount"] = bootCount++;
                json["time"] = time(nullptr);
            });
        Task::loop("telemetry", 8192, [this](Task& task) { publishTelemetry(task); });

        Serial.println("Application initialized in " + String(millis()) + " ms");
    }

    void registerTelemetryProvider(const String& name, TelemetryProvider& provider) {
        telemetryCollector.registerProvider(name, provider);
    }

    typedef std::function<void(const JsonObject&, JsonObject&)> CommandHandler;

    void registerCommand(const String& name, CommandHandler handler) {
        String suffix = "commands/" + name;
        mqtt.subscribe(suffix, MqttDriver::QoS::ExactlyOnce, [this, name, suffix, handler](const String&, const JsonObject& request) {
            // Clear topic
            mqtt.clear(suffix, MqttDriver::Retention::Retain, MqttDriver::QoS::ExactlyOnce);
            DynamicJsonDocument responseDoc(2048);
            auto response = responseDoc.to<JsonObject>();
            handler(request, response);
            if (response.size() > 0) {
                mqtt.publish("responses/" + name, responseDoc, MqttDriver::Retention::NoRetain, MqttDriver::QoS::ExactlyOnce);
            }
        });
    }

    void registerCommand(Command& command) {
        registerCommand(command.name, [&](const JsonObject& request, JsonObject& response) {
            command.handle(request, response);
        });
    }

private:
    enum class ApplicationState {
        BOOTING,
        NETWORK_CONNECTING,
        NETWORK_CONFIGURING,
        RTC_SYNCING,
        MQTT_CONNECTING,
        READY
    };

    void updateState() {
        ApplicationState newState;
        if (networkReadyState.isSet()) {
            // We have network
            if (rtcInSyncState.isSet()) {
                // We have some valid time
                if (mqttReadyState.isSet()) {
                    // We have MQTT conenction
                    newState = ApplicationState::READY;
                } else {
                    // We are waiting for MQTT connection
                    newState = ApplicationState::MQTT_CONNECTING;
                }
            } else {
                // We are waiting for NTP sync
                newState = ApplicationState::RTC_SYNCING;
            }
        } else {
            // We don't have network
            if (configPortalRunningState.isSet()) {
                // We are waiting for the user to configure the network
                newState = ApplicationState::NETWORK_CONFIGURING;
            } else {
                // We are waiting for network connection
                newState = ApplicationState::NETWORK_CONNECTING;
            }
        }

        if (newState != state) {
            Serial.printf("Application state changed from %d to %d\n", state, newState);
            state = newState;
            switch (newState) {
                case ApplicationState::NETWORK_CONNECTING:
                    statusLed.blink(milliseconds(200));
                    break;
                case ApplicationState::NETWORK_CONFIGURING:
                    statusLed.blinkPattern({
                        milliseconds(100),
                        milliseconds(-100),
                        milliseconds(100),
                        milliseconds(-100),
                        milliseconds(100),
                        milliseconds(-500),
                    });
                    break;
                case ApplicationState::RTC_SYNCING:
                    statusLed.blink(milliseconds(1000));
                    break;
                case ApplicationState::MQTT_CONNECTING:
                    statusLed.blink(milliseconds(2000));
                    break;
                case ApplicationState::READY:
                    statusLed.turnOn();
                    break;
            };
        }
        stateManager.awaitStateChange();
    }

    void publishTelemetry(Task& task) {
        mqtt.publish("telemetry", [&](JsonObject& json) { telemetryCollector.collect(json); });
        // TODO Configure telemetry heartbeat interval
        task.delayUntil(milliseconds(60000));
    }

    const String version;

    FileSystem& fs;
    TDeviceConfiguration deviceConfig;

    LedDriver& statusLed;
    ApplicationState state = ApplicationState::BOOTING;
    StateManager stateManager;
    StateSource networkReadyState = stateManager.createStateSource("network-ready");
    StateSource configPortalRunningState = stateManager.createStateSource("config-portal-running");
    StateSource rtcInSyncState = stateManager.createStateSource("rtc-in-sync");
    StateSource mdnsReadyState = stateManager.createStateSource("mdns-ready");
    StateSource mqttReadyState = stateManager.createStateSource("mqtt-ready");
    State applicationReadyState = stateManager.combineStates("application-ready",
        {
            networkReadyState,
            rtcInSyncState,
            mqttReadyState,
        });

    WiFiDriver wifi { networkReadyState, configPortalRunningState, deviceConfig.getHostname() };
#ifdef OTA_UPDATE
    // Only include OTA when needed for debugging
    OtaDriver ota { networkReadyState, deviceConfig.getHostname() };
#endif
    MdnsDriver mdns { networkReadyState, deviceConfig.getHostname(), "ugly-duckling", version, mdnsReadyState };
    RtcDriver rtc { networkReadyState, mdns, deviceConfig.ntp, rtcInSyncState };
    TelemetryCollector telemetryCollector;
    MqttDriver mqtt { networkReadyState, mdns, deviceConfig.mqtt, deviceConfig.instance.get(), mqttReadyState };

    EchoCommand echoCommand;
    RestartCommand restartCommand;
    SleepCommand sleepCommand;
    FileListCommand fileListCommand { fs };
    FileReadCommand fileReadCommand { fs };
    FileWriteCommand fileWriteCommand { fs };
    FileRemoveCommand fileRemoveCommand { fs };
    HttpUpdateCommand httpUpdateCommand { version };
};

}}    // namespace farmhub::kernel
