#include <memory>

#include <esp_err.h>

#include <Device.hpp>

#include <devices/UglyDucklingMk4.hpp>
#include <devices/UglyDucklingMk5.hpp>
#include <devices/UglyDucklingMk6.hpp>
#include <devices/UglyDucklingMk7.hpp>
#include <devices/UglyDucklingMk8.hpp>

using namespace farmhub::devices;

std::shared_ptr<DeviceFactory> createDeviceFactory(uint8_t* mac) {
    uint32_t designator = (mac[0] << 24) | (mac[1] << 16) | (mac[2] << 8) | mac[3];
    switch (designator) {
        case 0xd4f9846d:
        case 0x58cf79a3:
        case 0x7cdfa196:
            return std::make_shared<mk4::Factory>(Revision::Rev1);

        case 0xf412fa52:
            return std::make_shared<mk5::Factory>(Revision::Rev2);

        case 0x34851850:
            return std::make_shared<mk6::Factory>(Revision::Rev1);
        case 0xecda3b5b:
            return std::make_shared<mk6::Factory>(Revision::Rev2);
        case 0xf09e9e55:
            return std::make_shared<mk6::Factory>(Revision::Rev3);

        case 0x4827e282:
            return std::make_shared<mk7::Factory>(Revision::Rev1);

        case 0x240ac400:
            // Wokwi runs as MK6
            return std::make_shared<mk6::Factory>(Revision::Rev1);

        default:
            char macHex[18];
            (void) sprintf(macHex, "%02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            ESP_LOGE("DeviceFactory", "Unknown device with MAC address %s", macHex);
            esp_system_abort("Cannot find device based on MAC address");
    }
}

extern "C" void app_main() {
    auto macAddress = farmhub::kernel::getMacAddress();
    uint8_t rawMac[6];
    if (esp_read_mac(rawMac, ESP_MAC_WIFI_STA) != ESP_OK) {
        esp_system_abort("Failed to read MAC address, aborting");
    }

    auto factory = createDeviceFactory(rawMac);
    startDevice(factory);
}
