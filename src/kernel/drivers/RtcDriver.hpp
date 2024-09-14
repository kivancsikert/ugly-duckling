#pragma once

#include <WiFiUdp.h>
#include <chrono>
#include <time.h>

#include <NTPClient.h>

#include <kernel/Log.hpp>
#include <kernel/State.hpp>
#include <kernel/Task.hpp>

#include <kernel/drivers/MdnsDriver.hpp>

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

    RtcDriver(State& networkReady, WiFiDriver& wifi, MdnsDriver& mdns, const Config& ntpConfig, StateSource& rtcInSync)
        : networkReady(networkReady)
        , wifi(wifi)
        , mdns(mdns)
        , ntpConfig(ntpConfig)
        , rtcInSync(rtcInSync) {
        // TODO We should not need two separate tasks here
        Task::run("rtc-check", 2560, [this](Task& task) {
            while (true) {
                if (isTimeSet()) {
                    Log.info("RTC: time is set");
                    this->rtcInSync.set();
                    break;
                }
                task.delayUntil(1s);
            }
        });
        Task::run("ntp-sync", 4096, [this, &wifi](Task& task) {
            while (true) {
                {
                    WiFiToken connection(wifi);
                    ensureConfigured();
                    if (!ntpClient->forceUpdate()) {
                        // Attempt a retry, but with mDNS cache disabled
                        Log.error("RTC: NTP update failed, retrying in 10 seconds with mDNS cache disabled");
                        ntpClient = nullptr;
                        trustMdnsCache = false;
                        task.delay(10s);
                        continue;
                    }
                    trustMdnsCache = true;
                    setOrAdjustTime(system_clock::from_time_t(ntpClient->getEpochTime()));
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
    void ensureConfigured() {
        if (ntpClient != nullptr) {
            return;
        }

        if (ntpConfig.host.get().length() > 0) {
            Log.info("RTC: using NTP server %s from configuration",
                ntpConfig.host.get().c_str());
            ntpClient = new NTPClient(udp, ntpConfig.host.get().c_str());
        } else {
            MdnsRecord ntpServer;
            if (mdns.lookupService("ntp", "udp", ntpServer, trustMdnsCache)) {
                Log.info("RTC: using NTP server %s:%d (%s) from mDNS",
                    ntpServer.hostname.c_str(),
                    ntpServer.port,
                    ntpServer.ip.toString().c_str());
                ntpClient = new NTPClient(udp, ntpServer.ip);
            } else {
                Log.info("RTC: no NTP server configured, using default");
                ntpClient = new NTPClient(udp);
            }
        }

        // TODO Use built in configTime() instead
        //      We are using the external NTP client library, because the built in configTime() does not
        //      reliably update the time for some reason.
        ntpClient->begin();
    }

    static void setOrAdjustTime(time_point<system_clock> newEpochTime) {
        // Threshold for deciding between settimeofday and adjtime
        const auto threshold = 30s;

        // Get current time
        auto now = system_clock::now();

        // Calculate the difference
        auto difference = newEpochTime - now;

        if (difference.count() == 0) {
            Log.debug("RTC: Time is already correct");
        } else if (abs(difference) < threshold) {
            // If the difference is smaller, adjust the time gradually
            struct timeval adj = { .tv_sec = (time_t) duration_cast<seconds>(difference).count(), .tv_usec = 0 };
            adjtime(&adj, NULL);
            Log.debug("RTC: Adjusted time by %lld ms",
                duration_cast<milliseconds>(difference).count());
        } else {
            // If the difference is larger than the threshold, set the time directly
            struct timeval tv = { .tv_sec = (time_t) duration_cast<seconds>(newEpochTime.time_since_epoch()).count(), .tv_usec = 0 };
            settimeofday(&tv, NULL);
            Log.debug("RTC: Set time to %lld (from: %lld)",
                duration_cast<seconds>(newEpochTime.time_since_epoch()).count(),
                duration_cast<seconds>(now.time_since_epoch()).count());
        }
    }

    State& networkReady;
    WiFiDriver& wifi;
    MdnsDriver& mdns;
    const Config& ntpConfig;
    StateSource& rtcInSync;

    WiFiUDP udp;
    NTPClient* ntpClient = nullptr;
    bool trustMdnsCache = true;
};

}    // namespace farmhub::kernel::drivers
