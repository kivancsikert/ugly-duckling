#pragma once

#include <WiFiUdp.h>
#include <chrono>
#include <time.h>

#include <ArduinoLog.h>
#include <NTPClient.h>

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

    RtcDriver(State& networkReady, MdnsDriver& mdns, const Config& ntpConfig, StateSource& rtcInSync)
        : networkReady(networkReady)
        , mdns(mdns)
        , ntpConfig(ntpConfig)
        , rtcInSync(rtcInSync) {
        // TODO We should not need two separate tasks here
        Task::run("rtc-check", 2560, [this](Task& task) {
            while (true) {
                if (isTimeSet()) {
                    Log.infoln("RTC: time is set");
                    this->rtcInSync.set();
                    break;
                }
                task.delayUntil(1s);
            }
        });
        Task::run("ntp-sync", 4096, [this](Task& task) {
            while (true) {
                ensureConnected();
                if (ntpClient->forceUpdate()) {
                    trustMdnsCache = true;
                    setOrAdjustTime(ntpClient->getEpochTime());

                    // We are good for a while now
                    task.delay(1h);
                } else {
                    // Attempt a retry, but with mDNS cache disabled
                    Log.traceln("RTC: NTP update failed, retrying in 10 seconds with mDNS cache disabled");
                    ntpClient = nullptr;
                    trustMdnsCache = false;
                    task.delay(10s);
                }
            }
        });
    }

    static bool isTimeSet() {
        time_t now;
        time(&now);
        // The MCU boots with a timestamp of 0 seconds, so if the value is
        // much higher, then it means the RTC is set.
        return seconds(now) > hours((2022 - 1970) * 365 * 24);
    }

private:
    void ensureConnected() {
        networkReady.awaitSet();

        if (ntpClient != nullptr) {
            return;
        }

        if (ntpConfig.host.get().length() > 0) {
            Log.infoln("RTC: using NTP server %s from configuration",
                ntpConfig.host.get().c_str());
            ntpClient = new NTPClient(udp, ntpConfig.host.get().c_str());
        } else {
            MdnsRecord ntpServer;
            if (mdns.lookupService("ntp", "udp", ntpServer, trustMdnsCache)) {
                Log.infoln("RTC: using NTP server %s:%d (%p) from mDNS",
                    ntpServer.hostname.c_str(),
                    ntpServer.port,
                    ntpServer.ip);
                ntpClient = new NTPClient(udp, ntpServer.ip);
            } else {
                Log.infoln("RTC: no NTP server configured, using default");
                ntpClient = new NTPClient(udp);
            }
        }

        // TODO Use built in configTime() instead
        //      We are using the external NTP client library, because the built in configTime() does not
        //      reliably update the time for some reason.
        ntpClient->begin();
    }

    static void setOrAdjustTime(long newEpochTime) {
        // Threshold in seconds for deciding between settimeofday and adjtime
        const long threshold = 30;

        // Get current time
        time_t now;
        time(&now);

        // Calculate the difference
        long difference = newEpochTime - now;

        if (abs(difference) > threshold) {
            // If the difference is larger than the threshold, set the time directly
            struct timeval tv = { .tv_sec = newEpochTime, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            Log.traceln("RTC: Set time to %ld (from: %ld)",
                newEpochTime, now);
        } else if (difference != 0) {
            // If the difference is smaller, adjust the time gradually
            struct timeval adj = { .tv_sec = difference, .tv_usec = 0 };
            adjtime(&adj, NULL);
            Log.traceln("RTC: Adjusted time by %ld",
                difference);
        } else {
            Log.traceln("RTC: Time is already correct");
        }
    }

    State& networkReady;
    MdnsDriver& mdns;
    const Config& ntpConfig;
    StateSource& rtcInSync;

    WiFiUDP udp;
    NTPClient* ntpClient = nullptr;
    bool trustMdnsCache = true;
};

}    // namespace farmhub::kernel::drivers
