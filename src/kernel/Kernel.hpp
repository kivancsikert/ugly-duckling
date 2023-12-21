#pragma once

#include <functional>
#include <optional>

#include <freertos/FreeRTOS.h>

#include <ArduinoLog.h>

#include <kernel/Command.hpp>
#include <kernel/FileSystem.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
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
class Kernel;

static RTC_DATA_ATTR int bootCount = 0;

class MemoryTelemetryProvider : public TelemetryProvider {
public:
    void populateTelemetry(JsonObject& json) override {
        json["free-heap"] = ESP.getFreeHeap();
    }
};

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

class MqttTelemetryPublisher : public TelemetryPublisher {
public:
    MqttTelemetryPublisher(MqttDriver& mqtt, TelemetryCollector& telemetryCollector)
        : mqtt(mqtt)
        , telemetryCollector(telemetryCollector) {
    }

    void publishTelemetry() {
        mqtt.publish("telemetry", [&](JsonObject& json) { telemetryCollector.collect(json); });
    }

private:
    MqttDriver& mqtt;
    TelemetryCollector& telemetryCollector;
};

template <typename TDeviceConfiguration>
class Kernel {
public:
    Kernel(TDeviceConfiguration& deviceConfig, LedDriver& statusLed)
        : version(VERSION)
        , deviceConfig(deviceConfig)
        , statusLed(statusLed) {

        Log.infoln("Initializing FarmHub kernel version %s on %s instance '%s' with hostname '%s'",
            version.c_str(),
            deviceConfig.model.get().c_str(),
            deviceConfig.instance.get().c_str(),
            deviceConfig.getHostname());

        Task::loop("status-update", 4096, [this](Task&) { updateState(); });

#if defined(FARMHUB_DEBUG) || defined(FARMHUB_REPORT_MEMORY)
        registerTelemetryProvider("memory", memoryTelemetryProvider);
#endif

        registerCommand(echoCommand);
        registerCommand(pingCommand);
        // TODO Add reset-wifi command
        // registerCommand(resetWifiCommand);
        registerCommand(restartCommand);
        registerCommand(sleepCommand);
        registerCommand(fileListCommand);
        registerCommand(fileReadCommand);
        registerCommand(fileWriteCommand);
        registerCommand(fileRemoveCommand);
        registerCommand(httpUpdateCommand);
    }

    void begin() {
        kernelReadyState.awaitSet();

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
                // TODO Remove redundanty mentions of "ugly-duckling"
                json["app"] = "ugly-duckling";
                json["version"] = version;
                json["wakeup"] = esp_sleep_get_wakeup_cause();
                json["bootCount"] = bootCount++;
                json["time"] = time(nullptr);
            });
        Task::loop("telemetry", 8192, [this](Task& task) { publishTelemetry(task); });

        Log.infoln("Kernel ready in %d ms",
            millis());
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
    enum class KernelState {
        BOOTING,
        NETWORK_CONNECTING,
        NETWORK_CONFIGURING,
        RTC_SYNCING,
        MQTT_CONNECTING,
        READY
    };

    void updateState() {
        KernelState newState;
        if (networkReadyState.isSet()) {
            // We have network
            if (rtcInSyncState.isSet()) {
                // We have some valid time
                if (mqttReadyState.isSet()) {
                    // We have MQTT conenction
                    newState = KernelState::READY;
                } else {
                    // We are waiting for MQTT connection
                    newState = KernelState::MQTT_CONNECTING;
                }
            } else {
                // We are waiting for NTP sync
                newState = KernelState::RTC_SYNCING;
            }
        } else {
            // We don't have network
            if (configPortalRunningState.isSet()) {
                // We are waiting for the user to configure the network
                newState = KernelState::NETWORK_CONFIGURING;
            } else {
                // We are waiting for network connection
                newState = KernelState::NETWORK_CONNECTING;
            }
        }

        if (newState != state) {
            Log.traceln("Kernel state changed from %d to %d",
                state, newState);
            state = newState;
            switch (newState) {
                case KernelState::BOOTING:
                    statusLed.turnOff();
                    break;
                case KernelState::NETWORK_CONNECTING:
                    statusLed.blink(milliseconds(200));
                    break;
                case KernelState::NETWORK_CONFIGURING:
                    statusLed.blinkPattern({
                        milliseconds(100),
                        milliseconds(-100),
                        milliseconds(100),
                        milliseconds(-100),
                        milliseconds(100),
                        milliseconds(-500),
                    });
                    break;
                case KernelState::RTC_SYNCING:
                    statusLed.blink(milliseconds(1000));
                    break;
                case KernelState::MQTT_CONNECTING:
                    statusLed.blink(milliseconds(2000));
                    break;
                case KernelState::READY:
                    statusLed.turnOn();
                    break;
            };
        }
        stateManager.awaitStateChange();
    }

    void publishTelemetry(Task& task) {
        telemetryPublisher.publishTelemetry();
        // TODO Configure telemetry heartbeat interval
        task.delayUntil(milliseconds(60000));
    }

    const String version;

    FileSystem& fs { FileSystem::get() };
    TDeviceConfiguration& deviceConfig;

#if defined(FARMHUB_DEBUG) || defined(FARMHUB_REPORT_MEMORY)
    MemoryTelemetryProvider memoryTelemetryProvider;
#endif

    LedDriver& statusLed;
    KernelState state = KernelState::BOOTING;
    StateManager stateManager;
    StateSource networkReadyState = stateManager.createStateSource("network-ready");
    StateSource configPortalRunningState = stateManager.createStateSource("config-portal-running");
    StateSource rtcInSyncState = stateManager.createStateSource("rtc-in-sync");
    StateSource mdnsReadyState = stateManager.createStateSource("mdns-ready");
    StateSource mqttReadyState = stateManager.createStateSource("mqtt-ready");
    State kernelReadyState = stateManager.combineStates("kernel-ready",
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
    MqttTelemetryPublisher telemetryPublisher { mqtt, telemetryCollector };

    EchoCommand echoCommand;
    PingCommand pingCommand { telemetryPublisher };
    RestartCommand restartCommand;
    SleepCommand sleepCommand;
    FileListCommand fileListCommand { fs };
    FileReadCommand fileReadCommand { fs };
    FileWriteCommand fileWriteCommand { fs };
    FileRemoveCommand fileRemoveCommand { fs };
    HttpUpdateCommand httpUpdateCommand { version };
};

}}    // namespace farmhub::kernel
