#pragma once

#include <chrono>
#include <optional>
#include <time.h>

#include "esp_netif_sntp.h"
#include "esp_sntp.h"

#include <kernel/Configuration.hpp>
#include <kernel/State.hpp>
#include <kernel/Task.hpp>
#include <kernel/drivers/MdnsDriver.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;

namespace farmhub::kernel::drivers {

/**
 * @brief Ensures the real-time clock is properly set up and holds a real time.
 *
 * The driver runs two tasks:
 *
 * - The first task waits for the system time to be set. It sets the RTC in sync state when the time is set.
 *   This task is non-blocking, and will pass if the RTC is already set during a previous boot.
 *
 * - The second task configures the system time using the NTP server advertised by mDNS.
 *   This waits for mDNS to be ready, and then configures the system time.
 */
class RtcDriver {
public:
    class Config : public ConfigurationSection {
    public:
        Property<String> host { this, "host", "" };
    };

    RtcDriver(WiFiDriver& wifi, MdnsDriver& mdns, const Config& ntpConfig, StateSource& rtcInSync)
        : wifi(wifi)
        , mdns(mdns)
        , ntpConfig(ntpConfig)
        , rtcInSync(rtcInSync) {

        if (isTimeSet()) {
            LOGTI("rtc", "time is already set");
            rtcInSync.set();
        }

        Task::run("ntp-sync", 4096, [this, &wifi](Task& task) {
            while (true) {
                {
                    WiFiConnection connection(wifi);
                    if (updateTime()) {
                        trustMdnsCache = true;
                    } else {
                        // Attempt a retry, but with mDNS cache disabled
                        LOGTE("rtc", "NTP update failed, retrying in 10 seconds with mDNS cache disabled");
                        trustMdnsCache = false;
                        task.delay(10s);
                        continue;
                    }
                }

                // We are good for a while now
                task.delay(1h);
            }
        });
    }

    static bool isTimeSet() {
        auto now = system_clock::now();
        // This is 2022-01-01 00:00:00 UTC
        const time_point limit = system_clock::from_time_t(1640995200);
        // The MCU boots with a timestamp of 0 seconds, so if the value is
        // much higher, then it means the RTC is set.
        return now > limit;
    }

private:
    bool updateTime() {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        config.start = false;
        config.smooth_sync = true;
        config.server_from_dhcp = true;
        config.renew_servers_after_new_IP = true;
        config.wait_for_sync = true;
        config.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
        ESP_ERROR_CHECK(esp_netif_sntp_init(&config));

#ifdef WOKWI
        LOGTI("rtc", "using default NTP server for Wokwi");
#else
        // TODO Check this
        if (ntpConfig.host.get().length() > 0) {
            LOGTD("rtc", "using NTP server %s from configuration",
                ntpConfig.host.get().c_str());
            esp_sntp_setservername(0, ntpConfig.host.get().c_str());
        } else {
            MdnsRecord ntpServer;
            if (mdns.lookupService("ntp", "udp", ntpServer, trustMdnsCache)) {
                LOGTD("rtc", "using NTP server %s:%d (%s) from mDNS",
                    ntpServer.hostname.c_str(),
                    ntpServer.port,
                    ntpServer.ip.toString().c_str());
                auto serverIp = convertIp4(ntpServer.ip);
                esp_sntp_setserver(0, &serverIp);
            } else {
                LOGTD("rtc", "no NTP server configured, using default");
            }
        }
#endif
        printServers();

        bool success = false;
        ESP_ERROR_CHECK(esp_netif_sntp_start());

        auto ret = esp_netif_sntp_sync_wait(ticks(10s).count());
        // It's okay to assume RTC is _roughly_ in sync even if
        // we're not yet finished with smooth sync
        if (ret == ESP_OK || ret == ESP_ERR_NOT_FINISHED) {
            rtcInSync.set();
            success = true;
            LOGTD("rtc", "sync finished successfully");
        } else if (ret == ESP_ERR_TIMEOUT) {
            LOGTD("rtc", "waiting for time sync timed out");
        } else {
            LOGTD("rtc", "waiting for time sync returned 0x%x", ret);
        }

        esp_netif_sntp_deinit();
        return success;
    }

    static void printServers(void) {
        LOGD("List of configured NTP servers:");

        for (uint8_t i = 0; i < SNTP_MAX_SERVERS; ++i) {
            if (esp_sntp_getservername(i)) {
                LOGD(" - server %d: '%s'", i, esp_sntp_getservername(i));
            } else {
                char buff[48];
                ip_addr_t const* ip = esp_sntp_getserver(i);
                if (ipaddr_ntoa_r(ip, buff, 48) != NULL) {
                    LOGD(" - server %d: %s", i, buff);
                }
            }
        }
    }

    // TODO Use ESP-IDF's ip4_addr_t
    static ip_addr_t convertIp4(const IPAddress& ip) {
        ip_addr_t espIP;
        IP4_ADDR(&espIP.u_addr.ip4, ip[0], ip[1], ip[2], ip[3]);
        espIP.type = IPADDR_TYPE_V4;
        return espIP;
    }

    WiFiDriver& wifi;
    MdnsDriver& mdns;
    const Config& ntpConfig;
    StateSource& rtcInSync;

    bool trustMdnsCache = true;
};

}    // namespace farmhub::kernel::drivers
