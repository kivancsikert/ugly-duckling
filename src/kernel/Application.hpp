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

class Application;

static RTC_DATA_ATTR int bootCount = 0;

class DeviceConfiguration : public FileConfiguration {
public:
    DeviceConfiguration(
        FileSystem& fs,
        const String& defaultModel,
        size_t capacity = 2048)
        : FileConfiguration(fs, "device", "/device-config.json", capacity)
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

private:
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
    friend Application;
};

class ApplicationConfiguration : public FileConfiguration {
public:
    ApplicationConfiguration(FileSystem& fs, size_t capacity = 2048)
        : FileConfiguration(fs, "application", "/app-config.json", capacity) {
    }
};

class Application {
public:
    Application(FileSystem& fs, DeviceConfiguration& deviceConfig, gpio_num_t statusLedPin)
        : fs(fs)
        , version(VERSION)
        , deviceConfig(loadConfig(deviceConfig))
        , statusLed("status", statusLedPin) {

        Serial.printf("Initializing version %s on %s instance '%s' with hostname '%s'\n",
            version.c_str(),
            deviceConfig.model.get().c_str(),
            deviceConfig.instance.get().c_str(),
            deviceConfig.getHostname());

        Task::loop("status-update", [this](Task&) { updateState(); });
        Task::loop("telemetry", [this](Task& task) { publishTelemetry(task); });

        mqtt.registerCommand(echoCommand);
        // TODO Add ping command
        // mqtt.registerCommand(pingCommand);
        // TODO Add reset-wifi command
        // mqtt.registerCommand(resetWifiCommand);
        mqtt.registerCommand(restartCommand);
        mqtt.registerCommand(sleepCommand);
        mqtt.registerCommand(fileListCommand);
        mqtt.registerCommand(fileReadCommand);
        mqtt.registerCommand(fileWriteCommand);
        mqtt.registerCommand(fileRemoveCommand);
        mqtt.registerCommand(httpUpdateCommand);

        // TODO Init peripherals

        applicationReadyState.awaitSet();

        mqtt.publish(
            "init",
            [&](JsonObject& json) {
                // TODO Remove redundanty mentions of "ugly-duckling"
                json["type"] = "ugly-duckling";
                json["model"] = deviceConfig.model.get();
                json["instance"] = deviceConfig.instance.get();
                json["mac"] = DeviceConfiguration::getMacAddress();
                auto device = json.createNestedObject("deviceConfig");
                deviceConfig.store(device, false);
                json["app"] = "ugly-duckling";
                json["version"] = version;
                json["wakeup"] = esp_sleep_get_wakeup_cause();
                json["bootCount"] = bootCount++;
                json["time"] = time(nullptr);
            });

        Serial.println("Application initialized in " + String(millis()) + " ms");
    }

    void registerTelemetryProvider(const String& name, TelemetryProvider& provider) {
        telemetryCollector.registerProvider(name, provider);
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

    static DeviceConfiguration& loadConfig(DeviceConfiguration& deviceConfig) {
        deviceConfig.loadFromFileSystem();
        return deviceConfig;
    }

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
            state = newState;
            Serial.printf("Application state changed from %d to %d\n", state, newState);
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
        stateManager.waitStateChange();
    }

    void publishTelemetry(Task& task) {
        mqtt.publish("telemetry", [&](JsonObject& json) { telemetryCollector.collect(json); });
        task.delayUntil(milliseconds(5000));
    }

    FileSystem& fs;
    const String version;

    LedDriver statusLed;
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

    DeviceConfiguration deviceConfig;
    ApplicationConfiguration appConfig { fs };

    WiFiDriver wifi { networkReadyState, configPortalRunningState, deviceConfig.getHostname() };
#ifdef OTA_UPDATE
    // Only include OTA when needed for debugging
    OtaDriver ota { networkReadyState, deviceConfig.getHostname() };
#endif
    MdnsDriver mdns { networkReadyState, deviceConfig.getHostname(), "ugly-duckling", version, mdnsReadyState };
    RtcDriver rtc { networkReadyState, mdns, deviceConfig.ntp, rtcInSyncState };
    TelemetryCollector telemetryCollector;
    MqttDriver mqtt { networkReadyState, mdns, deviceConfig.mqtt, deviceConfig.instance.get(), appConfig, mqttReadyState };

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
