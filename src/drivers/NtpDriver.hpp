#pragma once

#include <time.h>

#include <Event.hpp>
#include <Task.hpp>

#include <drivers/MdnsDriver.hpp>

namespace farmhub { namespace device { namespace drivers {

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
    : public EventEmitter {
public:
    NtpDriver(MdnsDriver& mdns, EventGroupHandle_t eventGroup, int eventBit)
        : EventEmitter(eventGroup, eventBit)
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

    class NtpSyncTask : Task {
    public:
        NtpSyncTask(MdnsDriver& mdns)
            : Task("Sync time with NTP server")
            , mdns(mdns) {
        }

    protected:
        void run() override {
            Serial.println("NTP: Waiting for mDNS to be ready");
            mdns.waitFor();
            Serial.println("NTP: mDNS is ready");
            // TODO Handle lookup failure
            MdnsRecord ntpServer;
            if (mdns.lookupService("ntp", "udp", &ntpServer)) {
                Serial.println("NTP: configuring " + ntpServer.hostname + " (" + ntpServer.ip.toString() + ")");
                configTime(0, 0, ntpServer.hostname.c_str());
            }
        }

    private:
        MdnsDriver& mdns;
    };

    MdnsDriver& mdns;

    TimeCheckTask timeCheckTask { *this };
    NtpSyncTask ntpSyncTask { mdns };
};

}}}    // namespace farmhub::device::drivers
