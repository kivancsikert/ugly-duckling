#pragma once

#include <string>
#include <vector>

#include <esp_mac.h>

namespace farmhub::kernel {

static std::array<uint8_t, 6> getRawMacAddress() {
    static bool queried;
    static std::array<uint8_t, 6> mac {};
    if (!queried) {
        ESP_ERROR_THROW(esp_read_mac(mac.data(), ESP_MAC_WIFI_STA));
        queried = true;
    }
    return mac;
}

static const std::string& getMacAddress() {
    static std::string macAddress;
    if (macAddress.empty()) {
        auto rawMac = getRawMacAddress();
        char mac[24];
        (void) sprintf(mac, "%02x:%02x:%02x:%02x:%02x:%02x",
            rawMac[0], rawMac[1], rawMac[2], rawMac[3],
            rawMac[4], rawMac[5]);
        macAddress = mac;
    }
    return macAddress;
}

[[maybe_unused]]
static bool macAddressStartsWith(const std::vector<uint8_t>& prefix) {
    auto rawMac = getRawMacAddress();
    for (size_t i = 0; i < prefix.size(); i++) {
        if (rawMac[i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

}    // namespace farmhub::kernel
