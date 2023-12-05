#pragma once

#include <WiFiUdp.h>
#include <time.h>

#include <NTPClient.h>

#include <kernel/Event.hpp>
#include <kernel/Task.hpp>

#include <kernel/drivers/MdnsDriver.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

namespace farmhub { namespace kernel { namespace drivers {

/**
 * @brief Ensures the real-time clock is properly set up and holds a real time.
 *
 * The driver runs two tasks:
 *
 * - The first task waits for the system time to be set. It emits an event when the time is set.
 *   This task is non-blocking, and will pass if the RTC is already set during a previous boot.
 *
 * - The second task configures the system time using the NTP server advertised by mDNS.
 *   This waits for mDNS to be ready, and then configures the system time.
 */
class RtcDriver
    : public EventSource {
public:
    class Config : public NamedConfigurationSection {
    public:
        Config(ConfigurationSection* parent, const String& name)
            : NamedConfigurationSection(parent, name) {
        }

        Property<String> host { this, "host", "" };
    };

    RtcDriver(WiFiDriver& wifi, MdnsDriver& mdns, EventGroupHandle_t eventGroup, int eventBit, Config& ntpConfig)
        : EventSource(eventGroup, eventBit)
        , ntpSyncTask(wifi, mdns, ntpConfig) {
    }

private:
    class SystemTimeCheckTask : Task {
    public:
        SystemTimeCheckTask(RtcDriver& rtc)
            : Task("Check if system time is set to an actual value")
            , rtc(rtc) {
        }

    protected:
        void run() override {
            while (true) {
                time_t now;
                time(&now);
                // The MCU boots with a timestamp of 0, so if the value is
                // much higher, then it means the RTC is set.
                if (now > (2022 - 1970) * 365 * 24 * 60 * 60) {
                    Serial.println("Time configured, exiting task");
                    rtc.emitEvent();
                    break;
                }
                delayUntil(1000);
            }
        }

    private:
        RtcDriver& rtc;
    };

    class NtpSyncTask : IntermittentLoopTask {
    public:
        NtpSyncTask(WiFiDriver& wifi, MdnsDriver& mdns, Config& ntpConfig)
            : IntermittentLoopTask("Sync time with NTP server")
            , wifi(wifi)
            , mdns(mdns)
            , ntpConfig(ntpConfig) {
        }

    protected:
        void setup() override {
            if (ntpConfig.host.get().length() > 0) {
                Serial.println("NTP: using " + ntpConfig.host.get() + " from configuration");
                ntpClient = new NTPClient(udp, ntpConfig.host.get().c_str());
            } else {
                mdns.await();
                MdnsRecord ntpServer;
                if (mdns.lookupService("ntp", "udp", &ntpServer)) {
                    Serial.println("NTP: using " + ntpServer.hostname + ":" + String(ntpServer.port) + " (" + ntpServer.ip.toString() + ") from mDNS");
                    ntpClient = new NTPClient(udp, ntpServer.ip);
                } else {
                    Serial.println("NTP: using default server");
                    ntpClient = new NTPClient(udp);
                }
            }

            wifi.await();

            // TODO Use built in configTime() instead
            //      We are using the external NTP client library, because the built in configTime() does not
            //      reliably update the time for some reason.
            ntpClient->begin();
        }

        int loopAndDelay() override {
            if (ntpClient->forceUpdate()) {
                setOrAdjustTime(ntpClient->getEpochTime());

                // We are good for a while now
                return 60 * 60 * 1000;
            } else {
                // Attempt a retry
                return 10 * 1000;
            }
        }

    private:
        void setOrAdjustTime(long newEpochTime) {
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
                Serial.println("Set time to " + String(newEpochTime) + " (difference: " + String(difference));
            } else if (difference != 0) {
                // If the difference is smaller, adjust the time gradually
                struct timeval adj = { .tv_sec = difference, .tv_usec = 0 };
                adjtime(&adj, NULL);
                Serial.println("Adjusted time by " + String(difference));
            } else {
                Serial.println("Time is already correct");
            }
        }

        WiFiDriver& wifi;
        MdnsDriver& mdns;
        Config& ntpConfig;

        WiFiUDP udp;
        NTPClient* ntpClient;
    };

    SystemTimeCheckTask timeCheckTask { *this };
    NtpSyncTask ntpSyncTask;
};

}}}    // namespace farmhub::kernel::drivers
