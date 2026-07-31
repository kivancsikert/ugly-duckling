#pragma once

#ifndef UD_PLATFORM
#error "UD_PLATFORM is not defined — set it via DeviceCommon.cmake"
#endif

// Helper macros to convert macro to string
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#include <atomic>
#include <chrono>
#include <concepts>
#include <memory>
#include <string>

#include <driver/gpio.h>
#include <esp_app_desc.h>

static const char* const firmwareVersion = reinterpret_cast<const char*>(esp_app_get_description()->version);

#include <BatteryManager.hpp>
#include <Console.hpp>
#include <ConfigBootPlan.hpp>
#include <ConfigState.hpp>
#include <ConfigStateStore.hpp>
#include <CrashManager.hpp>
#include <DebugConsole.hpp>
#include <HardwareVersion.hpp>
#include <HttpUpdate.hpp>
#include <KernelStatus.hpp>
#include <Log.hpp>
#include <NvsConfiguration.hpp>
#include <NvsStore.hpp>
#include <StoredConfig.hpp>
#include <Strings.hpp>
#include <UpdateFilter.hpp>
#include <drivers/BleDriver.hpp>
#include <drivers/RtcDriver.hpp>
#include <mqtt/MqttDriver.hpp>
#include <mqtt/MqttLog.hpp>

#include <devices/DeviceConfiguration.hpp>
#include <devices/DeviceDefinition.hpp>
#include <functions/Function.hpp>
#include <peripherals/Peripheral.hpp>

using namespace std::chrono;
using namespace cornucopia::ugly_duckling::devices;
using namespace cornucopia::ugly_duckling::functions;
using namespace cornucopia::ugly_duckling::kernel;
using namespace cornucopia::ugly_duckling::peripherals;

#ifdef CONFIG_HEAP_TRACING
#include <esp_heap_trace.h>
#include <esp_system.h>

#define NUM_RECORDS 64
static heap_trace_record_t trace_record[NUM_RECORDS];    // This buffer must be in internal RAM

class HeapTrace {
public:
    HeapTrace() {
        ESP_ERROR_CHECK(heap_trace_start(HEAP_TRACE_LEAKS));
    }

    ~HeapTrace() {
        ESP_ERROR_CHECK(heap_trace_stop());
        heap_trace_dump();
        printf("Free heap: %lu\n", esp_get_free_heap_size());
    }
};
#endif

#ifdef CONFIG_HEAP_TASK_TRACKING
#include <esp_heap_task_info.h>

#define MAX_TASK_NUM 20     // Max number of per tasks info that it can store
#define MAX_BLOCK_NUM 20    // Max number of per block info that it can store

static size_t s_prepopulated_num = 0;
static heap_task_totals_t s_totals_arr[MAX_TASK_NUM];
static heap_task_block_t s_block_arr[MAX_BLOCK_NUM];

static void dumpPerTaskHeapInfo() {
    heap_task_info_params_t heapInfo = {
        .caps = { MALLOC_CAP_8BIT, MALLOC_CAP_32BIT },
        .mask = { MALLOC_CAP_8BIT, MALLOC_CAP_32BIT },
        .tasks = nullptr,
        .num_tasks = 0,
        .totals = s_totals_arr,
        .num_totals = &s_prepopulated_num,
        .max_totals = MAX_TASK_NUM,
        .blocks = s_block_arr,
        .max_blocks = MAX_BLOCK_NUM,
    };

    heap_caps_get_per_task_info(&heapInfo);

    for (int i = 0; i < *heapInfo.num_totals; i++) {
        auto taskInfo = heapInfo.totals[i];
        std::string taskName = taskInfo.task
            ? pcTaskGetName(taskInfo.task)
            : "Pre-Scheduler allocs";
        taskName.resize(configMAX_TASK_NAME_LEN, ' ');
        printf("Task %p: %s CAP_8BIT: %d, CAP_32BIT: %d, STACK LEFT: %ld\n",
            taskInfo.task,
            taskName.c_str(),
            taskInfo.size[0],
            taskInfo.size[1],
            uxTaskGetStackHighWaterMark2(taskInfo.task));
    }

    printf("\n\n");
}
#endif

/**
 * @brief Network configuration: MQTT broker settings, NTP, plus device instance and location.
 * Stored under the "network-config" key in NVS.
 */
struct NetworkConfig : MqttDriver::Config {
    Property<std::string> instance { this, "instance", getMacAddress() };
    Property<std::string> location { this, "location" };
    NamedConfigurationEntry<RtcDriver::Config> ntp { this, "ntp" };

    std::string getHostname() const {
        std::string hostname = instance.get();
        std::ranges::replace(hostname, ':', '-');
        std::erase(hostname, '?');
        return hostname;
    }
};

static void performFactoryReset(const std::shared_ptr<LedDriver>& statusLed, bool completeReset) {
    LOGI("Performing factory reset");

    statusLed->turnOn();
    Task::delay(1s);
    statusLed->turnOff();
    Task::delay(1s);
    statusLed->turnOn();

    LOGI(" - Deleting wifi NVS entries...");
    esp_wifi_restore();

    if (completeReset) {
        Task::delay(1s);
        statusLed->turnOff();
        Task::delay(1s);
        statusLed->turnOn();

        LOGI(" - Deleting all NVS config entries...");
        nvs_flash_erase();
    }

    LOGI(" - Restarting...");
    esp_restart();
}

std::shared_ptr<BatteryDriver> initBattery(const std::shared_ptr<DeviceDefinition>& deviceDefinition, const std::shared_ptr<I2CManager>& i2c) {
    auto battery = deviceDefinition->createBatteryDriver(i2c);
    if (battery != nullptr) {
        // If the battery voltage is below the device's threshold, we should not boot yet.
        // This is to prevent the device from booting and immediately shutting down
        // due to the high current draw of the boot process.
        auto voltage = battery->getVoltage();
        if (voltage != 0 && voltage < battery->parameters.bootThreshold) {
            ESP_LOGW("battery", "Battery voltage too low (%d mV < %d mV), entering deep sleep\n",
                voltage, battery->parameters.bootThreshold);
            enterLowPowerDeepSleep();
        }
    }
    return battery;
}

void initNvsFlash() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

std::shared_ptr<Watchdog> initWatchdog(seconds timeout) {
    return std::make_shared<Watchdog>("watchdog", timeout, true, [](WatchdogState state) {
        if (state == WatchdogState::TimedOut) {
            LOGE("Watchdog timed out");
            esp_system_abort("Watchdog timed out");
        }
    });
}

std::shared_ptr<MqttRoot> initMqtt(const std::shared_ptr<ModuleStates>& states, const std::string& clientId, const std::shared_ptr<NetworkConfig>& networkConfig, StateSource& mqttReady) {
    // NetworkConfig inherits from MqttDriver::Config, so we can upcast
    auto mqttConfig = std::static_pointer_cast<MqttDriver::Config>(networkConfig);
    auto mqtt = std::make_shared<MqttDriver>(states->networkReady, mqttConfig, clientId, mqttReady);
    const std::string& location = networkConfig->location.get();
    return std::make_shared<MqttRoot>(mqtt, (location.empty() ? "" : location + "/") + "devices/ugly-duckling/" + networkConfig->instance.get());
}

void registerBasicCommands(const std::shared_ptr<MqttRoot>& mqttRoot) {
    mqttRoot->registerCommand("restart", [](const JsonObject&, JsonObject&) {
        printf("Restarting...\n");
        (void) fflush(stdout);
        fsync(fileno(stdout));
        esp_restart();
    });
    mqttRoot->registerCommand("sleep", [](const JsonObject& request, JsonObject& _response) {
        seconds duration = seconds(request["duration"].as<int64_t>());
        esp_sleep_enable_timer_wakeup((microseconds(duration)).count());
        LOGI("Sleeping deep for %lld seconds",
            duration.count());
        esp_deep_sleep_start();
    });
}

void registerNvsCommands(const std::shared_ptr<MqttRoot>& mqttRoot) {
    mqttRoot->registerCommand("nvs/list", [](const JsonObject& request, JsonObject& response) {
        const char* ns = request["namespace"] | "config";
        NvsStore store(ns);
        JsonArray entries = response["entries"].to<JsonArray>();
        store.list([entries](const std::string& key) {
            auto entry = entries.add<JsonObject>();
            entry["key"] = key;
        });
    });
    mqttRoot->registerCommand("nvs/read", [](const JsonObject& request, JsonObject& response) {
        const char* ns = request["namespace"] | "config";
        NvsStore store(ns);
        auto key = request["key"].as<std::string>();
        LOGI("Reading NVS key '%s' from namespace '%s'", key.c_str(), ns);
        response["key"] = key;
        JsonDocument valueDoc;
        if (store.getJson(key, valueDoc)) {
            response["value"].set(valueDoc.as<JsonVariant>());
        } else {
            response["error"] = "Key not found";
        }
    });
    mqttRoot->registerCommand("nvs/write", [](const JsonObject& request, JsonObject& response) {
        const char* ns = request["namespace"] | "config";
        NvsStore store(ns);
        auto key = request["key"].as<std::string>();
        LOGI("Writing NVS key '%s' to namespace '%s'", key.c_str(), ns);
        response["key"] = key;
        store.setJson(key, request["value"]);
        response["written"] = true;
    });
    mqttRoot->registerCommand("nvs/remove", [](const JsonObject& request, JsonObject& response) {
        const char* ns = request["namespace"] | "config";
        NvsStore store(ns);
        auto key = request["key"].as<std::string>();
        LOGI("Removing NVS key '%s' from namespace '%s'", key.c_str(), ns);
        response["key"] = key;
        if (store.remove(key)) {
            response["removed"] = true;
        } else {
            response["error"] = "Key not found or could not be removed";
        }
    });
}

void registerHttpUpdateCommand(const std::shared_ptr<MqttRoot>& mqttRoot, const std::shared_ptr<NvsStore>& nvs) {
    mqttRoot->registerCommand("update", [nvs](const JsonObject& request, JsonObject& response) {
        if (!request["url"].is<std::string>()) {
            response["failure"] = "Command contains no URL";
            return;
        }
        std::string url = request["url"];
        if (url.empty()) {
            response["failure"] = "Command contains empty url";
            return;
        }
        HttpUpdater::startUpdate(url, nvs);
        response["success"] = true;
    });
}

/**
 * @brief Subscribes to `update` (NoRetain, QoS 2) -- the only config-in path
 * (docs/Configuration.md, "BOOT, SYNC, UPDATE"). Filters incoming configurations by the fingerprints
 * already held (device + every live function), then either persists-and-reboots (device
 * configuration changed -- boot re-derives everything, including which functions exist, from NVS)
 * or hot-reloads each changed function live via FunctionRegistry::reconfigure. Phase 1 has no
 * atomicity/rejection: a faulty function body is logged and skipped, exactly like any other
 * faulty configuration today. A successful (functions-only) hot-reload pushes to
 * syncTriggerQueue so the SYNC task re-advertises the new fingerprints without waiting for the
 * next reconnect; a device-changed update reboots instead, so SYNC follows from the next boot's
 * connection-established trigger.
 */
void registerUpdateHandler(
    const std::shared_ptr<MqttRoot>& mqttRoot,
    const std::shared_ptr<NvsStore>& configNvs,
    const std::string& deviceConfirmedFingerprint,
    const std::shared_ptr<FunctionRegistry>& functionRegistry,
    const std::shared_ptr<CopyQueue<bool>>& syncTriggerQueue) {
    mqttRoot->subscribe("update", QoS::ExactlyOnce, [configNvs, deviceConfirmedFingerprint, functionRegistry, syncTriggerQueue](const std::string&, const JsonObject& request) {
        JsonObjectConst configurations = request["configurations"];
        if (configurations.isNull()) {
            LOGW("Ignoring update with no 'configurations'");
            return;
        }

        std::unordered_map<std::string, std::string> heldFingerprints;
        for (const auto& [name, entry] : functionRegistry->manifest()) {
            heldFingerprints[name] = entry.fingerprint;
        }
        heldFingerprints[DEVICE_CONFIGURATION_NAME] = deviceConfirmedFingerprint;

        FilteredUpdate update = filterUpdate(configurations, heldFingerprints);
        if (update.changed.empty()) {
            LOGD("Ignoring update: nothing differs from what's currently held");
            return;
        }

        if (update.deviceChanged) {
            // Do not hot-reload -- let the boot sequence apply the whole set, since a device
            // change can restructure which functions exist. No staging/atomicity yet (Phase 1):
            // a boot that then fails is unrecoverable exactly as today.
            for (const auto& entry : update.changed) {
                if (entry.name == DEVICE_CONFIGURATION_NAME) {
                    StoredConfig(configNvs, "device-config").store(entry.envelope);
                } else {
                    functionRegistry->persist(entry.name, entry.envelope);
                }
            }
            LOGI("Device configuration changed via update, rebooting to apply");
            esp_restart();
            return;
        }

        for (const auto& entry : update.changed) {
            try {
                functionRegistry->reconfigure(entry.name, entry.envelope);
            } catch (const std::exception& e) {
                LOGE("Failed to apply configuration update for '%s': %s", entry.name.c_str(), e.what());
            }
        }

        syncTriggerQueue->overwrite(true);
    });
}

/**
 * @brief Publishes `sync` (NoRetain, QoS 2): the manifest of function fingerprints/requestedAt the
 * device has applied and booted with (docs/Configuration.md, "BOOT, SYNC, UPDATE"), read straight
 * from FunctionRegistry's in-memory state -- proof-of-apply, never re-derived from NVS. The
 * `device` entry is Phase 3 (reporting it requires the two-slot confirmed/requested machinery).
 *
 * `pendingRejection` carries this boot's rejection code (if any) so it can ride on the first SYNC
 * published after boot, alongside BOOT (docs/Configuration.md, "Rejection reporting") -- it is
 * consumed (reset to nullopt) right here, so a later SYNC in the same boot session doesn't repeat
 * it.
 */
void publishSync(
    const std::shared_ptr<MqttRoot>& mqttRoot,
    const std::shared_ptr<FunctionRegistry>& functionRegistry,
    const std::shared_ptr<std::optional<RejectionCode>>& pendingRejection) {
    std::optional<RejectionCode> rejection = *pendingRejection;
    *pendingRejection = std::nullopt;
    mqttRoot->publish(
        "sync",
        [functionRegistry, rejection](JsonObject& json) {
            auto configurations = json["configurations"].to<JsonObject>();
            writeSyncManifest(configurations, functionRegistry->manifest());
            if (rejection) {
                json["rejection"] = static_cast<int>(*rejection);
            }
        },
        Retention::NoRetain, QoS::ExactlyOnce);
}

/**
 * @brief Dedicated task that publishes SYNC whenever triggered via syncTriggerQueue -- a
 * single-element overwrite queue, so a flurry of triggers (reconnects, or a post-UPDATE request)
 * coalesces into one pending SYNC. Awaits kernelReady before publishing so SYNC only ever reports
 * a fully-booted configuration set: if MQTT connects before boot finishes, the publish simply
 * waits. There is no boot-time SYNC -- this task is the only publisher.
 *
 * Any further triggers that arrive while we are waiting on kernelReady (e.g. a flaky reconnect
 * during a slow boot) are drained right before publishing rather than left for the next loop
 * iteration: since publishSync() always reads FunctionRegistry's current state rather than a
 * snapshot from trigger time, the about-to-happen publish already answers them, so leaving them
 * queued would only cause an immediate, redundant re-publish straight after this one.
 *
 * `pendingRejection` is populated by the caller (`startDevice()`) strictly before `kernelReady` is
 * set, so `awaitSet()` below establishes happens-after visibility of it regardless of how early a
 * trigger arrives.
 */
void initSyncTask(
    const std::shared_ptr<MqttRoot>& mqttRoot,
    const std::shared_ptr<CopyQueue<bool>>& syncTriggerQueue,
    const std::shared_ptr<ModuleStates>& states,
    const std::shared_ptr<FunctionRegistry>& functionRegistry,
    const std::shared_ptr<std::optional<RejectionCode>>& pendingRejection) {
    Task::loop("sync", 4096, [mqttRoot, syncTriggerQueue, states, functionRegistry, pendingRejection](Task&) {
        syncTriggerQueue->take();
        states->kernelReady.awaitSet();
        syncTriggerQueue->clear();
        publishSync(mqttRoot, functionRegistry, pendingRejection);
    });
}

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

        ble->setBatteryLevel(static_cast<uint8_t>(batteryManager->getPercentage()));

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
            telemetryCollector->collect(features); }, Retention::NoRetain, QoS::AtLeastOnce);

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

enum class InitState : std::uint8_t {
    Success = 0,
    PeripheralError = 1,
    FunctionError = 2,
};

template <std::derived_from<DeviceDefinition> TDeviceDefinition>
static void startDevice() {
    auto deviceDefinition = std::make_shared<TDeviceDefinition>();
    LOGD("Starting %s revision %d device",
        deviceDefinition->model.c_str(), deviceDefinition->revision);

    auto resetReason = esp_reset_reason();
    LOGD("Restarting after reset reason: %d", resetReason);

    auto i2c = std::make_shared<I2CManager>();
    auto battery = initBattery(deviceDefinition, i2c);

    initNvsFlash();

    // Install GPIO ISR service
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

#ifdef CONFIG_HEAP_TRACING
    ESP_ERROR_CHECK(heap_trace_init_standalone(trace_record, NUM_RECORDS));
#endif

    auto configNvs = std::make_shared<NvsStore>("config");

    LOGTV(NVS, "NVS configurations:");
    configNvs->list([](const std::string& key) {
        LOGTV(NVS, " - %s", key.c_str());
    });

    JsonDocument networkConfigRaw;
    auto networkConfig = loadConfigFromNvs<NetworkConfig>(configNvs, "network-config", networkConfigRaw);

    // Two-slot confirmed/requested atomicity (docs/Configuration.md, "The confirmed/requested state
    // machine"). Nothing populates `requested` yet -- that is item 4's job (staging an
    // UPDATE's device-configuration change instead of writing straight through) -- so in practice
    // bootPlan.slotToLoad is always nullopt today and the branches below fall through to exactly
    // today's flat device-config/function-cfg storage. The strict/revert machinery is real and
    // boot-wired regardless, exercised directly by ConfigStateStoreTest.cpp until item 4 lands.
    auto configStateNvs = std::make_shared<NvsStore>("config-state");
    ConfigStateStore configStateStore(configStateNvs);
    ConfigState configState = configStateStore.load();
    BootPlan bootPlan = decideBootPlan(configState);
    if (bootPlan.stateToPersistBeforeLoad) {
        configStateStore.save(*bootPlan.stateToPersistBeforeLoad);
        configState = *bootPlan.stateToPersistBeforeLoad;
    }

    // Device configuration is stored as a verbatim envelope like any other reconciled
    // configuration (docs/Configuration.md, "Storage: envelopes and slots"), so its fingerprint is
    // available to the `update` handler below without recomputing anything. When bootPlan selects a
    // slot (a `requested` set being attempted, or a previously-committed `confirmed` slot), device
    // and function configuration load from that slot's own namespaces instead of the flat storage.
    std::shared_ptr<NvsStore> deviceConfigNvs = configNvs;
    std::string deviceConfigKey = "device-config";
    std::shared_ptr<NvsStore> functionsConfigSlotNvs;
    if (bootPlan.slotToLoad) {
        const std::string slotSuffix = "-" + toString(*bootPlan.slotToLoad);
        deviceConfigNvs = std::make_shared<NvsStore>("config" + slotSuffix);
        deviceConfigKey = "device";
        functionsConfigSlotNvs = std::make_shared<NvsStore>("function-cfg" + slotSuffix);
    }
    StoredConfig deviceStoredConfig(deviceConfigNvs, deviceConfigKey);
    auto deviceConfig = std::make_shared<DeviceConfiguration>();
    JsonDocument deviceConfigRaw;
    if (deviceStoredConfig.hasValue()) {
        deviceConfigRaw = deviceStoredConfig.data();
        deviceConfig->load(deviceConfigRaw.as<JsonObject>());
    }

    const std::string modelWithRevision = deviceDefinition->model + " (rev" + std::to_string(deviceDefinition->revision) + ")";

    auto watchdog = initWatchdog(deviceConfig->watchdogTimeout.get());

    auto powerManager = std::make_shared<PowerManager>(deviceConfig->sleepWhenIdle.get());

    auto logRecords = std::make_shared<Queue<LogRecord>>("logs",
#ifdef UD_DEBUG
        128
#else
        32
#endif
    );
    ConsoleProvider::init(logRecords, deviceConfig->publishLogs.get());

    const auto& macAddress = getMacAddress();
    const auto& hardwareVersion = getHardwareVersion();

    LOGD("\n"
         "   _   _       _         ____             _    _ _\n"
         "  | | | | __ _| |_   _  |  _ \\ _   _  ___| | _| (_)_ __   __ _\n"
         "  | | | |/ _` | | | | | | | | | | | |/ __| |/ / | | '_ \\ / _` |\n"
         "  | |_| | (_| | | |_| | | |_| | |_| | (__|   <| | | | | | (_| |\n"
         "   \\___/ \\__, |_|\\__, | |____/ \\__,_|\\___|_|\\_\\_|_|_| |_|\\__, |\n"
         "         |___/   |___/                                    |___/ %s\n",
        firmwareVersion);
    LOGI("Initializing ugly duckling firmware version %s on %s instance '%s' with hostname '%s' and MAC address %s",
        firmwareVersion,
        modelWithRevision.c_str(),
        networkConfig->instance.get().c_str(),
        networkConfig->getHostname().c_str(),
        macAddress.c_str());

    auto statusLed = std::make_shared<LedDriver>("status", deviceDefinition->statusPin);
    auto states = std::make_shared<ModuleStates>();
    KernelStatusTask::init(statusLed, states);

    // Init BLE (optional — disabled via deviceConfig->bleEnabled; compiled out entirely on
    // platforms without CONFIG_BT_NIMBLE_ENABLED, e.g. Spinach — see docs/specs/Bluetooth.md
    // "Platform support decision")
    std::shared_ptr<BleDriver> ble;
#ifdef CONFIG_BT_NIMBLE_ENABLED
    if (deviceConfig->bleEnabled.get()) {
        LOGI("BLE enabled, starting NimBLE stack");
        auto bleNvs = std::make_shared<NvsStore>("ble");
        ble = std::make_shared<NimBleDriver>(
            networkConfig->getHostname(),
            "Ugly Duckling " + modelWithRevision,
            firmwareVersion,
            macAddress,
            bleNvs,
            deviceConfig->bleAdvInterval.get());
    } else {
        LOGI("BLE disabled, using no-op driver");
        ble = std::make_shared<BleDriver>();
    }
#else
    ble = std::make_shared<BleDriver>();
#endif

    // Init WiFi
    auto wifi = std::make_shared<WiFiDriver>(
        states->networkConnecting,
        states->networkReady,
        states->configPortalRunning,
        networkConfig->getHostname());

    auto telemetryPublishQueue = std::make_shared<CopyQueue<bool>>("telemetry-publish", 1);
    auto telemetryPublisher = std::make_shared<TelemetryPublisher>(telemetryPublishQueue);

    // Init switch and button handling
    auto switches = std::make_shared<SwitchManager>();
    switches->registerSwitch({ .name = "factory-reset",
        .pin = deviceDefinition->bootPin,
        .mode = SwitchMode::PullUp,
        .onDisengaged = [statusLed, telemetryPublisher](const SwitchEvent& event) {
            auto duration = event.timeSinceLastChange;
            if (duration >= 15s) {
                LOGI("Factory reset triggered after %lld ms", duration.count());
                performFactoryReset(statusLed, true);
            } else if (duration >= 5s) {
                LOGI("WiFi reset triggered after %lld ms", duration.count());
                performFactoryReset(statusLed, false);
            } else if (duration >= 200ms) {
                LOGD("Publishing telemetry after %lld ms", duration.count());
                telemetryPublisher->requestTelemetryPublishing();
            }
        } });

    // Init battery management
    auto shutdownManager = std::make_shared<ShutdownManager>();
    std::shared_ptr<BatteryManager> batteryManager;
    if (battery != nullptr) {
        LOGD("Battery configured");
        batteryManager = std::make_shared<BatteryManager>(battery, shutdownManager);
    } else {
        LOGD("No battery configured");
    }

#ifdef UD_DEBUG
    new DebugConsole(batteryManager, wifi, ble);
#endif

    // Init real time clock
    auto rtc = std::make_shared<RtcDriver>(wifi->getNetworkReady(), networkConfig->ntp.get(), states->rtcInSync);
    ble->setOnTimeReceived([rtc](time_t utcTime) { rtc->setTime(utcTime); });
    ble->setOnWifiScanRequested([wifi, ble]() {
        wifi->startWifiScan([ble](const std::vector<WifiApRecord>& records) {
            ble->setScanResults(records);
        });
    });
    ble->setOnWifiCredentialsReceived([wifi](const std::string& ssid, const std::string& password) {
        wifi->setCredentials(ssid, password);
    });
    ble->setOnWifiControlReceived([wifi](const std::string& cmd) {
        if (cmd == "disconnect") {
            wifi->disconnect();
        } else if (cmd == "disable") {
            wifi->disable();
        }
    });
    wifi->setOnStatusChanged([ble](const std::string& status) {
        ble->setWifiStatus(status);
    });

    // Init MQTT connection
    auto clientId = "ugly-duckling-" + macAddress;
    auto mqttRoot = initMqtt(states, clientId, networkConfig, states->mqttReady);
    MqttLog::init(deviceConfig->publishLogs.get(), logRecords, mqttRoot);
    registerBasicCommands(mqttRoot);
    registerNvsCommands(mqttRoot);

    // SYNC trigger: a single-element overwrite queue coalesces every successful (re)connection
    // (docs/Configuration.md, "BOOT, SYNC, UPDATE") plus post-UPDATE requests into one pending
    // publish. The connected-listener callback must not publish inline (it runs on the MQTT
    // event-loop task, which publish() itself enqueues onto and waits for), so it only overwrites
    // the queue; initSyncTask below does the actual publish, from its own task.
    auto syncTriggerQueue = std::make_shared<CopyQueue<bool>>("sync-trigger", 1);
    mqttRoot->mqtt->onConnected([syncTriggerQueue]() {
        syncTriggerQueue->overwrite(true);
    });

    // Holds this boot's rejection code (if any) for the SYNC task to attach to the first SYNC it
    // publishes -- populated below, once the strict-boot outcome is known, strictly before
    // kernelReady is set (docs/Configuration.md, "Rejection reporting").
    auto pendingSyncRejection = std::make_shared<std::optional<RejectionCode>>();

    // Handle any pending HTTP update (will reboot if update was required and was successful)
    registerHttpUpdateCommand(mqttRoot, configNvs);
    HttpUpdater::performPendingHttpUpdateIfNecessary(configNvs, wifi, watchdog);

    auto peripheralsNvs = std::make_shared<NvsStore>("perf-state");
    auto pulseCounterManager = std::make_shared<PulseCounterManager>();
    auto pwm = std::make_shared<PwmManager>();
    auto telemetryCollector = std::make_shared<TelemetryCollector>();

    // Init peripherals
    auto peripheralServices = PeripheralServices {
        .i2c = i2c,
        .mqttDeviceRoot = mqttRoot,
        .nvs = peripheralsNvs,
        .pulseCounterManager = pulseCounterManager,
        .pwmManager = pwm,
        .switches = switches,
        .telemetryPublisher = telemetryPublisher,
    };
    auto peripheralManager = std::make_shared<PeripheralManager>(telemetryCollector, peripheralServices);
    shutdownManager->registerShutdownListener([peripheralManager]() {
        peripheralManager->shutdown();
    });
    deviceDefinition->registerPeripheralFactories(peripheralManager, peripheralServices, deviceConfig);

    // Init functions
    auto functionServices = FunctionServices {
        .peripherals = peripheralManager,
        .telemetryPublisher = telemetryPublisher,
    };
    auto functionsConfigNvs = functionsConfigSlotNvs ? functionsConfigSlotNvs : std::make_shared<NvsStore>("function-cfg");
    auto functionRegistry = std::make_shared<FunctionRegistry>(functionsConfigNvs, functionServices);
    shutdownManager->registerShutdownListener([functionRegistry]() {
        functionRegistry->shutdown();
    });
    deviceDefinition->registerFunctionFactories(functionRegistry);
    registerUpdateHandler(mqttRoot, configNvs, deviceStoredConfig.fingerprint(), functionRegistry, syncTriggerQueue);
    initSyncTask(mqttRoot, syncTriggerQueue, states, functionRegistry, pendingSyncRejection);

    // Init telemetry
    mqttRoot->registerCommand("ping", [telemetryPublisher](const JsonObject&, JsonObject& response) {
        telemetryPublisher->requestTelemetryPublishing();
        response["pong"] = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    });

    // We want RTC to be in sync before we start setting up peripherals
    states->rtcInSync.awaitSet();

    InitState initState = InitState::Success;

    // Init peripherals
    JsonDocument peripheralsInitDoc;
    auto peripheralsInitJson = peripheralsInitDoc.to<JsonArray>();

    auto builtInPeripheralsSettings = deviceDefinition->getBuiltInPeripherals();
    LOGD("Loading configuration for %d built-in peripherals",
        builtInPeripheralsSettings.size());
    for (auto& builtInPeripheralSettings : builtInPeripheralsSettings) {
        if (!peripheralManager->createPeripheral(builtInPeripheralSettings, peripheralsInitJson)) {
            initState = InitState::PeripheralError;
        }
    }

    const auto& peripheralsSettings = deviceConfig->peripherals.get();
    LOGI("Loading configuration for %d user-configured peripherals",
        peripheralsSettings.size());
    for (const auto& peripheralSettings : peripheralsSettings) {
        if (!peripheralManager->createPeripheral(peripheralSettings.get(), peripheralsInitJson)) {
            initState = InitState::PeripheralError;
        }
    }

    // Start ULP pulse counter after all channels have been registered by peripherals above.
    pulseCounterManager->start();

    JsonDocument functionsInitDoc;
    auto functionsInitJson = functionsInitDoc.to<JsonArray>();
    const auto& functionsSettings = deviceConfig->functions.get();
    LOGI("Loading configuration for %d user-configured functions",
        functionsSettings.size());
    for (const auto& functionSettings : functionsSettings) {
        if (!functionRegistry->createFunction(functionSettings.get(), functionsInitJson)) {
            initState = InitState::FunctionError;
        }
    }

    // Booting a `requested` set is strict (docs/Configuration.md, "The confirmed/requested state
    // machine"): unlike `confirmed` (or the empty-slot default), which boots best-effort regardless
    // of errors, any peripheral/function apply error here is a detected failure -- revert to `confirmed` and
    // reboot immediately, never reaching the `boot`/`sync` publishes below for this failed attempt.
    // A hard crash before reaching this point is caught on the *next* boot instead, since the
    // pending -> attempted transition was already persisted above, before this attempt started.
    if (bootPlan.strict) {
        bool success = (initState == InitState::Success);
        ConfigState outcome = recordStrictBootOutcome(configState, *bootPlan.slotToLoad, success, RejectionCode::Internal);
        configStateStore.save(outcome);
        if (!success) {
            LOGE("Requested configuration failed to apply (state=%d); reverting and rebooting",
                static_cast<int>(initState));
            esp_restart();
            return;
        }
        configState = outcome;
        LOGI("Requested configuration applied successfully; committed slot '%s' as confirmed",
            toString(*bootPlan.slotToLoad).c_str());
    }

    // A rejection recorded by this boot's revert (or an earlier one still unreported) is echoed on
    // this boot's BOOT message *and* on the first SYNC this boot publishes (docs/Configuration.md,
    // "Rejection reporting"): BOOT because it fires deterministically on every boot, unlike SYNC
    // which waits on kernelReady + a live MQTT connection, and SYNC too since it's what most
    // clients are already watching for reconciliation state. Cleared from config-state immediately
    // (not after confirmed delivery), matching this system's existing no-delivery-guarantees
    // posture; `pendingSyncRejection` carries the in-memory copy to the SYNC task, which consumes
    // it after its first publish so later SYNCs in this boot session don't repeat it.
    std::optional<RejectionCode> rejectionToReport = configState.rejection;
    if (rejectionToReport) {
        ConfigState cleared = configState;
        cleared.rejection.reset();
        configStateStore.save(cleared);
    }
    *pendingSyncRejection = rejectionToReport;

    initTelemetryPublishTask(deviceConfig->publishInterval.get(), watchdog, mqttRoot, batteryManager, powerManager, wifi, ble, telemetryCollector, telemetryPublishQueue);

    // Enable power saving once we are done initializing
    WiFiDriver::setPowerSaveMode(deviceConfig->sleepWhenIdle.get());

    // BOOT carries diagnostics and per-peripheral/function error feedback, but no configuration
    // bodies (docs/Configuration.md, "BOOT, SYNC, UPDATE") -- fingerprints are reported separately
    // by SYNC (initSyncTask above), gated on kernelReady. `rejection` is present only when a
    // requested-set revert (this boot or an earlier, unreported one) left one recorded.
    mqttRoot->publish(
        "boot",
        [resetReason, macAddress, networkConfig, initState, peripheralsInitJson, functionsInitJson, powerManager, deviceDefinition, hardwareVersion, rejectionToReport](JsonObject& json) {
            json["model"] = deviceDefinition->model;
            json["revision"] = deviceDefinition->revision;
            json["platform"] = UD_PLATFORM;
            json["instance"] = networkConfig->instance.get();
            json["mac"] = macAddress;
            if (hardwareVersion.has_value()) {
                json["batch"] = hardwareVersion->batch;
                json["serial"] = hardwareVersion->serial;
            }
            json["version"] = firmwareVersion;
#ifdef UD_DEBUG
            json["debug"] = true;
#else
            json["debug"] = false;
#endif
            json["reset"] = resetReason;
            json["wakeup"] = esp_sleep_get_wakeup_causes();
            json["bootCount"] = bootCount++;
            json["time"] = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
            json["state"] = static_cast<int>(initState);
            json["peripherals"].to<JsonArray>().set(peripheralsInitJson);
            json["functions"].to<JsonArray>().set(functionsInitJson);
            json["sleepWhenIdle"] = powerManager->sleepWhenIdle;
            if (rejectionToReport) {
                json["rejection"] = static_cast<int>(*rejectionToReport);
            }

            CrashManager::handleCrashReport(json);
        },
        Retention::NoRetain, QoS::AtLeastOnce, 5s);

    states->kernelReady.set();

    LOGI("Device ready in %.2f s (kernel version %s on %s instance '%s' with hostname '%s' and IP '%s', SSID '%s', current time is %lld)",
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() / 1000.0,
        firmwareVersion,
        modelWithRevision.c_str(),
        networkConfig->instance.get().c_str(),
        networkConfig->getHostname().c_str(),
        wifi->getIp().value_or("<no-ip>").c_str(),
        wifi->getSsid().value_or("<no-ssid>").c_str(),
        duration_cast<seconds>(system_clock::now().time_since_epoch()).count());

#ifdef CONFIG_HEAP_TASK_TRACKING
    Task::loop("task-heaps", 4096, [](Task& task) {
        dumpPerTaskHeapInfo();
        Task::delay(ticks(5s));
    });
#endif

    vTaskDelete(nullptr);
}
