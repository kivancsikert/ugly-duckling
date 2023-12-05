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
 * @brief Ensures the system time is synchronized with an NTP server.
 *
 * The driver runs two tasks:
 *
 * - The first task waits for the system time to be set. It emits an event when the time is set.
 *   This task is non-blocking, and will pass if the RTC is already set during a previous boot.
 *
 * - The second task configures the system time using the NTP server advertised by mDNS.
 *   This waits for mDNS to be ready, and then configures the system time.
 */
class NtpDriver
    : public EventSource {
public:
    NtpDriver(WiFiDriver& wifi, MdnsDriver& mdns, EventGroupHandle_t eventGroup, int eventBit)
        : EventSource(eventGroup, eventBit)
        , wifi(wifi)
        , mdns(mdns) {
    }

private:
    class TimeCheckTask : Task {
    public:
        TimeCheckTask(NtpDriver& ntp)
            : Task("Check for synced time")
            , ntp(ntp) {
        }

    protected:
        void run() override {
            while (true) {
                time_t now;
                time(&now);
                if (now > (2022 - 1970) * 365 * 24 * 60 * 60) {
                    Serial.println("Time configured, exiting task");
                    ntp.emitEvent();
                    break;
                }
                delayUntil(1000);
            }
        }

    private:
        NtpDriver& ntp;
    };

    class NtpSyncTask : IntermittentLoopTask {
    public:
        NtpSyncTask(WiFiDriver& wifi, MdnsDriver& mdns)
            : IntermittentLoopTask("Sync time with NTP server")
            , wifi(wifi)
            , mdns(mdns) {
        }

    protected:
        void setup() override {
            // TODO Allow configuring NTP servers manually
            mdns.await();
            MdnsRecord mdnsRecord;
            if (mdns.lookupService("ntp", "udp", &mdnsRecord)) {
                Serial.println("NTP: using " + mdnsRecord.hostname + ":" + String(mdnsRecord.port) + " (" + mdnsRecord.ip.toString() + ")");
                ntpClient = new NTPClient(udp, mdnsRecord.ip);
            } else {
                Serial.println("NTP: using default server");
                ntpClient = new NTPClient(udp);
            }

            wifi.await();

            // TODO Use built in configTime() instead
            //      We are using the external NTP client library, because the built in configTime() does not
            //      reliably update the time for some reason.
            ntpClient->begin();
        }

        int loopAndDelay() override {
            if (ntpClient->forceUpdate()) {
                struct timeval tv;
                tv.tv_sec = ntpClient->getEpochTime();    // Set the seconds
                tv.tv_usec = 0;                           // Set the microseconds to zero
                settimeofday(&tv, NULL);                  // Set the system time
                // We are good for a while now
                return 60 * 60 * 1000;
            } else {
                // Attempt a retry
                return 10 * 1000;
            }
        }

    private:
        WiFiDriver& wifi;
        MdnsDriver& mdns;
        WiFiUDP udp;
        NTPClient* ntpClient;
    };

    WiFiDriver& wifi;
    MdnsDriver& mdns;

    TimeCheckTask timeCheckTask { *this };
    NtpSyncTask ntpSyncTask { wifi, mdns };
};

}}}    // namespace farmhub::kernel::drivers
