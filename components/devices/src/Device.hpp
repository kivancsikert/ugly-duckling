#pragma once

#ifndef UD_PLATFORM
#error "UD_PLATFORM is not defined — set it via DeviceCommon.cmake"
#endif

// Helper macros to convert macro to string
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#include <chrono>
#include <concepts>
#include <cstdint>
#include <memory>
#include <string>

#include <esp_app_desc.h>

static const char* const firmwareVersion = reinterpret_cast<const char*>(esp_app_get_description()->version);

#include <Console.hpp>
#include <CrashManager.hpp>
#include <DebugConsole.hpp>
#include <FirmwareRollback.hpp>
#include <HardwareVersion.hpp>
#include <HttpUpdate.hpp>
#include <Log.hpp>
#include <ShutdownManager.hpp>
#include <Strings.hpp>
#include <config/ConfigBootPlan.hpp>
#include <config/NvsConfiguration.hpp>
#include <drivers/WiFiDriver.hpp>
#include <mqtt/MqttLog.hpp>

#include <devices/DeviceConfiguration.hpp>
#include <devices/DeviceDefinition.hpp>
#include <peripherals/Peripheral.hpp>

#include <BootHelpers.hpp>
#include <ConfigUpdate.hpp>
#include <HeapTrace.hpp>
#include <MqttCommands.hpp>
#include <NetworkConfig.hpp>
#include <SyncPublisher.hpp>
#include <TelemetryTask.hpp>

using namespace std::chrono;
using namespace cornucopia::ugly_duckling::devices;
using namespace cornucopia::ugly_duckling::functions;
using namespace cornucopia::ugly_duckling::kernel;
using namespace cornucopia::ugly_duckling::kernel::config;
using namespace cornucopia::ugly_duckling::peripherals;

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
    // machine"). A device-changed UPDATE (registerUpdateHandler below) is what populates
    // `requested`, staging a self-contained set into the free slot; the strict/revert machinery
    // here is what boots it. `configStateStore` is a shared_ptr so registerUpdateHandler's
    // subscription closure can load/save it live, long after this function's locals would
    // otherwise have gone out of scope (startDevice() never returns).
    auto configStateNvs = std::make_shared<NvsStore>("config-state");
    auto configStateStore = std::make_shared<ConfigStateStore>(configStateNvs);
    ConfigState configState = configStateStore->load();
    BootPlan bootPlan = decideBootPlan(configState);

    LOGD("Booting from slot '%s', strict: %s, crash recovery checkpoint to persist: %s",
        bootPlan.slotToLoad ? toString(*bootPlan.slotToLoad).c_str() : "(none)",
        bootPlan.strict ? "true" : "false",
        bootPlan.crashRecoveryCheckpoint ? "true": "false");

    if (bootPlan.crashRecoveryCheckpoint) {
        configStateStore->save(*bootPlan.crashRecoveryCheckpoint);
        configState = *bootPlan.crashRecoveryCheckpoint;
    }

    // Device configuration is stored as a verbatim envelope like any other reconciled
    // configuration (docs/Configuration.md, "Storage: envelopes and slots"), so its fingerprint is
    // available to the `update` handler below without recomputing anything. There is no flat/unslotted
    // storage any more: a device with no confirmed slot (a freshly minted device, or one migrating
    // from before this firmware) boots exactly like an empty slot -- defaults, no functions -- and
    // reconciles from scratch via an empty SYNC prompting the server to re-push everything (see
    // docs/specs/config-reconciliation.md, "Migration" -> "A missing/absent confirmed slot is the
    // one bootstrap path").
    std::shared_ptr<NvsStore> deviceConfigNvs;
    if (bootPlan.slotToLoad) {
        deviceConfigNvs = std::make_shared<NvsStore>("config-" + toString(*bootPlan.slotToLoad));
    }
    auto deviceConfig = std::make_shared<DeviceConfiguration>();
    std::string deviceConfirmedFingerprint;
    std::string deviceConfirmedRequestedAt;
    if (deviceConfigNvs) {
        StoredConfig deviceStoredConfig(deviceConfigNvs, DEVICE_CONFIGURATION_NAME);
        if (deviceStoredConfig.hasValue()) {
            JsonDocument deviceConfigRaw = deviceStoredConfig.data();
            deviceConfig->load(deviceConfigRaw.as<JsonObject>());
            deviceConfirmedFingerprint = deviceStoredConfig.fingerprint();
            deviceConfirmedRequestedAt = deviceStoredConfig.requestedAt();
        }
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
    switches->registerSwitch({
        .name = "factory-reset",
        .pin = deviceDefinition->bootPin,
        .mode = SwitchMode::PullUp,
        .onDisengaged = [statusLed, deviceDefinition, telemetryPublisher](const SwitchEvent& event) {
            auto duration = event.timeSinceLastChange;
            if (duration >= 30s) {
                LOGI("Factory reset triggered after %lld ms", duration.count());
                performFactoryReset(statusLed, true);
            } else if (duration >= 5s) {
                LOGI("WiFi reset triggered after %lld ms", duration.count());
                performFactoryReset(statusLed, false);
            } else if (duration >= 1000ms) {
                LOGD("Publishing telemetry after %lld ms", duration.count());
                telemetryPublisher->requestTelemetryPublishing();
            } else {
                deviceDefinition->handleShortButtonPress(duration);
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
    auto pendingConfigRejection = std::make_shared<std::optional<RejectionCode>>();

    // Firmware rejection: same pattern as pendingConfigRejection, but for a failed firmware
    // download/install (docs/specs/firmware-update-via-sync-update.md, "Rejection reporting").
    // Populated here from performPendingHttpUpdateIfNecessary()'s return value, consumed once
    // by the first SYNC this boot publishes.
    auto pendingFirmwareRejection = std::make_shared<std::optional<RejectionCode>>();

    // Handle any pending HTTP update (will reboot if update was required and was successful)
    registerHttpUpdateCommand(mqttRoot, configNvs);
    auto firmwareDownloadRejection = HttpUpdater::performPendingHttpUpdateIfNecessary(configNvs, wifi, watchdog);

    // Detect whether the bootloader rolled back from a failed OTA partition. This and a failed
    // download cannot co-occur: a failed download never writes a new partition, so there's nothing
    // for the bootloader to roll back from. If both somehow fire (defense in depth), the rollback
    // takes priority — it's the more severe signal.
    auto rollback = detectAndClearRollback();

    // Thread whichever rejection source fired (at most one) to BOOT and SYNC
    if (rollback) {
        *pendingFirmwareRejection = rollback->rejectionCode;
    } else {
        *pendingFirmwareRejection = firmwareDownloadRejection;
    }

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
    // Function configuration lives in the same namespace as the device document (deviceConfigNvs,
    // "config-<slot>") -- there is no separate function-cfg namespace any more (docs/Configuration.md,
    // "Storage: envelopes and slots"). Null only when there's no confirmed slot at all, in which case
    // deviceConfig->functions is empty too, so no function is ever created against it.
    auto functionRegistry = std::make_shared<FunctionRegistry>(deviceConfigNvs, functionServices);
    shutdownManager->registerShutdownListener([functionRegistry]() {
        functionRegistry->shutdown();
    });
    deviceDefinition->registerFunctionFactories(functionRegistry);
    FunctionManifestEntry deviceManifestEntry {
        .fingerprint = deviceConfirmedFingerprint,
        .requestedAt = deviceConfirmedRequestedAt,
    };
    registerUpdateHandler(mqttRoot, deviceConfirmedFingerprint, functionRegistry, configStateStore, syncTriggerQueue, configNvs, firmwareVersion);
    initSyncTask(mqttRoot, syncTriggerQueue, states, functionRegistry, deviceManifestEntry, pendingConfigRejection, pendingFirmwareRejection, firmwareVersion);

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
        configStateStore->save(outcome);
        if (!success) {
            LOGE("Requested configuration failed to apply (state=%d); reverting and rebooting",
                static_cast<int>(initState));
            delayedRestart();
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
    // posture; `pendingConfigRejection` carries the in-memory copy to the SYNC task, which consumes
    // it after its first publish so later SYNCs in this boot session don't repeat it.
    std::optional<RejectionCode> rejectionToReport = configState.rejection;
    if (rejectionToReport) {
        ConfigState cleared = configState;
        cleared.rejection.reset();
        configStateStore->save(cleared);
    }
    *pendingConfigRejection = rejectionToReport;

    initTelemetryPublishTask(deviceConfig->publishInterval.get(), watchdog, mqttRoot, batteryManager, powerManager, wifi, ble, telemetryCollector, telemetryPublishQueue);

    // Enable power saving once we are done initializing
    WiFiDriver::setPowerSaveMode(deviceConfig->sleepWhenIdle.get());

    // BOOT carries diagnostics and per-peripheral/function error feedback, but no configuration
    // bodies (docs/Configuration.md, "BOOT, SYNC, UPDATE") -- fingerprints are reported separately
    // by SYNC (initSyncTask above), gated on kernelReady. `rejection` is present only when a
    // requested-set revert (this boot or an earlier, unreported one) left one recorded. Published at
    // QoS 2, consistent with every other outbound topic (sync, log, responses) -- also closes off the
    // same duplicate-resend risk described on initTelemetryPublishTask above, even though BOOT's own
    // payload has no delta/counter state that a duplicate would corrupt.
    mqttRoot->publish(
        "boot",
        [resetReason, macAddress, networkConfig, initState, peripheralsInitJson, functionsInitJson, powerManager, deviceDefinition, hardwareVersion, rejectionToReport, firmwareDownloadRejection, rollback](JsonObject& json) {
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
            // firmwareRejection: either a failed download or a rollback (at most one)
            if (rollback) {
                json["firmwareRejection"] = static_cast<int>(rollback->rejectionCode);
            } else if (firmwareDownloadRejection) {
                json["firmwareRejection"] = static_cast<int>(*firmwareDownloadRejection);
            }

            // When a rollback was detected, attribute any coredump to the failed partition's
            // version rather than the currently running (rolled-back-to) firmwareVersion
            std::optional<std::string> rolledBackFromVersion;
            if (rollback) {
                rolledBackFromVersion = rollback->failedVersion;
            }
            CrashManager::handleCrashReport(json, rolledBackFromVersion);
        },
        Retention::NoRetain, QoS::ExactlyOnce, 5s);

    states->kernelReady.set();

    // Confirm this firmware as valid, cancelling any pending rollback. Must happen after
    // kernelReady — until this call, the bootloader considers a freshly-flashed partition
    // PENDING_VERIFY and will automatically revert if the device resets.
    confirmFirmwareValid();

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
