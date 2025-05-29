#pragma once

#include <chrono>
#include <optional>
#include <time.h>

#include "esp_netif_sntp.h"
#include "esp_sntp.h"

#include <Configuration.hpp>
#include <State.hpp>
#include <Task.hpp>
#include <drivers/MdnsDriver.hpp>

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
        Property<std::string> host { this, "host", "" };
    };

    RtcDriver(State& networkReady, std::shared_ptr<MdnsDriver> mdns, const std::shared_ptr<Config> ntpConfig, StateSource& rtcInSync)
        : mdns(mdns)
        , ntpConfig(ntpConfig)
        , rtcInSync(rtcInSync) {

        if (isTimeSet()) {
            LOGTI(Tag::RTC, "time is already set");
            rtcInSync.set();
        }

        Task::run("ntp-sync", 4096, [this, &networkReady](Task& task) {
            while (true) {
                {
                    networkReady.awaitSet();
                    if (updateTime()) {
                        trustMdnsCache = true;
                    } else {
                        // Attempt a retry, but with mDNS cache disabled
                        LOGTE(Tag::RTC, "NTP update failed, retrying in 10 seconds with mDNS cache disabled");
                        trustMdnsCache = false;
                        Task::delay(10s);
                        continue;
                    }
                }

                // We are good for a while now
                Task::delay(1h);
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

    State& getInSync() {
        return rtcInSync;
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
        LOGTI(Tag::RTC, "using default NTP server for Wokwi");
#else
        // TODO Check this
        if (ntpConfig->host.get().length() > 0) {
            LOGTD(Tag::RTC, "using NTP server %s from configuration",
                ntpConfig->host.get().c_str());
            esp_sntp_setservername(0, ntpConfig->host.get().c_str());
        } else {
            MdnsRecord ntpServer;
            if (mdns->lookupService("ntp", "udp", ntpServer, trustMdnsCache)) {
                LOGTD(Tag::RTC, "using NTP server %s from mDNS",
                    ntpServer.toString().c_str());
                esp_sntp_setserver(0, (const ip_addr_t*) &ntpServer.ip);
            } else {
                LOGTD(Tag::RTC, "no NTP server configured, using default");
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
            LOGTD(Tag::RTC, "sync finished successfully");
        } else if (ret == ESP_ERR_TIMEOUT) {
            LOGTD(Tag::RTC, "waiting for time sync timed out");
        } else {
            LOGTD(Tag::RTC, "waiting for time sync returned 0x%x", ret);
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

    const std::shared_ptr<MdnsDriver> mdns;
    const std::shared_ptr<Config> ntpConfig;
    StateSource& rtcInSync;

    bool trustMdnsCache = true;
};

}    // namespace farmhub::kernel::drivers
