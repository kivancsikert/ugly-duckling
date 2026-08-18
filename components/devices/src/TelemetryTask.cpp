#include "Watchdog.hpp"
#include "mqtt/MqttRoot.hpp"
#include "BatteryManager.hpp"
#include "PowerManager.hpp"
#include "Queue.hpp"
#include "drivers/WiFiDriver.hpp"
#include "drivers/BleDriver.hpp"
#include "Telemetry.hpp"
#include "Task.hpp"
#include "mqtt/MqttDriver.hpp"
#include <TelemetryTask.hpp>

#include <bits/chrono.h>
#include <chrono>
#include <cstdint>
#include <esp_heap_caps.h>
#include <memory>

using namespace std::chrono;
using namespace cornucopia::ugly_duckling::kernel;

/**
 * QoS 2 (not 1) matters here: esp-mqtt's outbox resends an unacked PUBLISH verbatim (same
 * packet id, DUP set) if the ack doesn't arrive within its retransmit timeout while the
 * connection stays up, and at QoS 1 the broker has no obligation to dedup that resend before
 * fanning it out to subscribers. Several features (e.g. flow-meter volume, reported as a delta
 * since last report with the on-device counter reset to 0 right after) aren't idempotent under
 * a duplicate delivery, so QoS 2's packet-id-keyed handshake is what actually prevents the
 * resend from being delivered twice (issue #579).
 */
void initTelemetryPublishTask(
    milliseconds publishInterval,
    const std::shared_ptr<Watchdog>& watchdog,
    const std::shared_ptr<MqttRoot>& mqttRoot,
    const std::shared_ptr<BatteryManager>& batteryManager,
    const std::shared_ptr<PowerManager>& powerManager,
    const std::shared_ptr<WiFiDriver>& wifi,
    const std::shared_ptr<BleDriver>& ble,
    const std::shared_ptr<TelemetryCollector>& telemetryCollector,
    const std::shared_ptr<CopyQueue<bool>>& telemetryPublishQueue) {
    Task::loop("telemetry", 8192, [publishInterval, watchdog, mqttRoot, batteryManager, powerManager, wifi, ble, telemetryCollector, telemetryPublishQueue](Task& task) {
        task.markWakeTime();

        if (batteryManager != nullptr) {
            ble->setBatteryLevel(static_cast<uint8_t>(batteryManager->getPercentage()));
        }

        mqttRoot->publish("telemetry", [batteryManager, powerManager, wifi, mqttRoot, telemetryCollector](JsonObject& telemetry) {
            telemetry["uptime"] = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
            telemetry["timestamp"] = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

            if (batteryManager != nullptr) {
                auto battery = telemetry["battery"].to<JsonObject>();
                battery["voltage"] = static_cast<double>(batteryManager->getVoltage()) / 1000.0;    // Convert to volts
                battery["percentage"] = batteryManager->getPercentage();
                auto current = batteryManager->getCurrent();
                if (current.has_value()) {
                    battery["current"] = *current;
                }
                auto timeToEmpty = batteryManager->getTimeToEmpty();
                if (timeToEmpty.has_value()) {
                    battery["time-to-empty"] = timeToEmpty->count();
                }
            }

            auto wifiData = telemetry["wifi"].to<JsonObject>();
            wifi->populateTelemetry(wifiData);

            auto mqttData = telemetry["mqtt"].to<JsonObject>();
            mqttRoot->mqtt->populateTelemetry(mqttData);

            auto memoryData = telemetry["memory"].to<JsonObject>();
            memoryData["free-heap"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            memoryData["min-heap"] = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);

            auto powerManagementData = telemetry["pm"].to<JsonObject>();
            powerManager->populateTelemetry(powerManagementData);

            auto features = telemetry["features"].to<JsonArray>();
            telemetryCollector->collect(features); }, Retention::NoRetain, QoS::ExactlyOnce);

        // Signal that we are still alive
        watchdog->restart();

        // We always wait at least this much between telemetry updates
        const auto debounceInterval = 500ms;
        // Delay without updating last wake time
        Task::delay(task.ticksUntil(debounceInterval));

        // Allow other tasks to trigger telemetry updates
        auto timeout = task.ticksUntil(publishInterval - debounceInterval);
        telemetryPublishQueue->pollIn(timeout);
    });
}
