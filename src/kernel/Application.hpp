#pragma once

#include <freertos/FreeRTOS.h>

#include <kernel/Command.hpp>
#include <kernel/FileSystem.hpp>
#include <kernel/drivers/MdnsDriver.hpp>
#include <kernel/drivers/MqttDriver.hpp>
#include <kernel/drivers/OtaDriver.hpp>
#include <kernel/drivers/RtcDriver.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

#include <version.h>

namespace farmhub { namespace kernel {

using namespace farmhub::kernel::drivers;

class Application;

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
    Application(FileSystem& fs, DeviceConfiguration& deviceConfig)
        : fs(fs)
        , version(VERSION)
        , deviceConfig(loadConfig(deviceConfig)) {

        Serial.printf("Initializing version %s on %s instance '%s' with hostname '%s'\n",
            version.c_str(),
            deviceConfig.model.get().c_str(),
            deviceConfig.instance.get().c_str(),
            deviceConfig.getHostname());

        mqtt.registerCommand(echoCommand);
        // TODO Add ping command
        // mqtt.registerCommand(pingCommand);
        // TODO Add reset-wifi command
        // mqtt.registerCommand(resetWifiCommand);
        mqtt.registerCommand(restartCommand);
        mqtt.registerCommand(fileListCommand);
        mqtt.registerCommand(fileReadCommand);
        mqtt.registerCommand(fileWriteCommand);
        mqtt.registerCommand(fileRemoveCommand);
        mqtt.registerCommand(httpUpdateCommand);

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
                // TODO Handle sleep / wakeup
                // json["wakeup"] = event.source;
            });
    }

private:
    static DeviceConfiguration& loadConfig(DeviceConfiguration& deviceConfig) {
        deviceConfig.loadFromFileSystem();
        return deviceConfig;
    }

    FileSystem& fs;
    const String version;

    DeviceConfiguration deviceConfig;
    ApplicationConfiguration appConfig { fs };
    EventGroupHandle_t eventGroup { xEventGroupCreate() };
    WiFiDriver wifi { eventGroup, WIFI_CONFIGURED_BIT };
#ifdef OTA_UPDATE
    // Only include OTA when needed for debugging
    OtaDriver ota { wifi, deviceConfig.getHostname() };
#endif
    MdnsDriver mdns { wifi, deviceConfig.getHostname(), "ugly-duckling", version, eventGroup, MDNS_CONFIGURED_BIT };
    RtcDriver rtc { wifi, mdns, eventGroup, NTP_SYNCED_BIT, deviceConfig.ntp };
    MqttDriver mqtt { wifi, mdns, deviceConfig.mqtt, deviceConfig.instance.get(), appConfig };

    EchoCommand echoCommand;
    RestartCommand restartCommand;
    FileListCommand fileListCommand { fs };
    FileReadCommand fileReadCommand { fs };
    FileWriteCommand fileWriteCommand { fs };
    FileRemoveCommand fileRemoveCommand { fs };
    HttpUpdateCommand httpUpdateCommand { version };

    static const int WIFI_CONFIGURED_BIT = 1;
    static const int NTP_SYNCED_BIT = 2;
    static const int MDNS_CONFIGURED_BIT = 3;
};

}}    // namespace farmhub::kernel
