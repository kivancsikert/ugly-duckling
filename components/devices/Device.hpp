#pragma once

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

static const char* const farmhubVersion = reinterpret_cast<const char*>(esp_app_get_description()->version);

#include <BatteryManager.hpp>
#include <Console.hpp>
#include <CrashManager.hpp>
#include <DebugConsole.hpp>
#include <HttpUpdate.hpp>
#include <KernelStatus.hpp>
#include <Log.hpp>
#include <Strings.hpp>
#include <mqtt/MqttLog.hpp>

#include <devices/DeviceDefinition.hpp>
#include <devices/DeviceSettings.hpp>
#include <functions/Function.hpp>
#include <peripherals/Peripheral.hpp>

using namespace farmhub::devices;
using namespace farmhub::functions;
using namespace farmhub::kernel;
using namespace farmhub::peripherals;

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

static void performFactoryReset(const std::shared_ptr<LedDriver>& statusLed, bool completeReset) {
    LOGI("Performing factory reset");

    statusLed->turnOn();
    Task::delay(1s);
    statusLed->turnOff();
    Task::delay(1s);
    statusLed->turnOn();

    if (completeReset) {
        Task::delay(1s);
        statusLed->turnOff();
        Task::delay(1s);
        statusLed->turnOn();

        LOGI(" - Deleting the file system...");
        FileSystem::format();
    }

    LOGI(" - Clearing NVS...");
    nvs_flash_erase();

    LOGI(" - Restarting...");
    esp_restart();
}

template <class TDeviceDefinition>
std::shared_ptr<BatteryDriver> initBattery(const std::shared_ptr<I2CManager>& i2c) {
    auto battery = TDeviceDefinition::createBatteryDriver(i2c);
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

std::shared_ptr<Watchdog> initWatchdog() {
    return std::make_shared<Watchdog>("watchdog", 5min, true, [](WatchdogState state) {
        if (state == WatchdogState::TimedOut) {
            LOGE("Watchdog timed out");
            esp_system_abort("Watchdog timed out");
        }
    });
}

template <std::derived_from<ConfigurationSection> TConfiguration>
std::shared_ptr<TConfiguration> loadConfig(const std::shared_ptr<FileSystem>& fs, const std::string& path) {
    auto config = std::make_shared<TConfiguration>();
    // TODO This should just be a "load()" call
    ConfigurationFile<TConfiguration> configFile(fs, path, config);
    return config;
}

std::shared_ptr<MqttRoot> initMqtt(const std::shared_ptr<ModuleStates>& states, const std::shared_ptr<MdnsDriver>& mdns, const std::shared_ptr<MqttDriver::Config>& mqttConfig, const std::string& instance, const std::string& location) {
    auto mqtt = std::make_shared<MqttDriver>(states->networkReady, mdns, mqttConfig, instance, states->mqttReady);
    return std::make_shared<MqttRoot>(mqtt, (location.empty() ? "" : location + "/") + "devices/ugly-duckling/" + instance);
}

void registerBasicCommands(const std::shared_ptr<MqttRoot>& mqttRoot) {
    mqttRoot->registerCommand("restart", [](const JsonObject&, JsonObject&) {
        printf("Restarting...\n");
        (void) fflush(stdout);
        fsync(fileno(stdout));
        esp_restart();
    });
    mqttRoot->registerCommand("sleep", [](const JsonObject& request, JsonObject& /*response*/) {
        seconds duration = seconds(request["duration"].as<int64_t>());
        esp_sleep_enable_timer_wakeup((microseconds(duration)).count());
        LOGI("Sleeping deep for %lld seconds",
            duration.count());
        esp_deep_sleep_start();
    });
}

void registerFileCommands(const std::shared_ptr<MqttRoot>& mqttRoot, const std::shared_ptr<FileSystem>& fs) {
    mqttRoot->registerCommand("files/list", [fs](const JsonObject&, JsonObject& response) {
        JsonArray files = response["files"].to<JsonArray>();
        fs->readDir("/", [files](const std::string& name, off_t size) {
            auto file = files.add<JsonObject>();
            file["name"] = name;
            file["size"] = size;
        });
    });
    mqttRoot->registerCommand("files/read", [fs](const JsonObject& request, JsonObject& response) {
        std::string path = request["path"];
        if (!path.starts_with("/")) {
            path = "/" + path;
        }
        LOGI("Reading %s",
            path.c_str());
        response["path"] = path;
        if (fs->exists(path)) {
            response["size"] = fs->size(path);
            auto contents = fs->readAll(path);
            if (contents.has_value()) {
                response["contents"] = contents.value();
            }
        } else {
            response["error"] = "File not found";
        }
    });
    mqttRoot->registerCommand("files/write", [fs](const JsonObject& request, JsonObject& response) {
        std::string path = request["path"];
        if (!path.starts_with("/")) {
            path = "/" + path;
        }
        LOGI("Writing %s",
            path.c_str());
        std::string contents = request["contents"];
        response["path"] = path;
        size_t written = fs->writeAll(path, contents);
        response["written"] = written;
    });
    mqttRoot->registerCommand("files/remove", [fs](const JsonObject& request, JsonObject& response) {
        std::string path = request["path"];
        if (!path.starts_with("/")) {
            path = "/" + path;
        }
        LOGI("Removing %s",
            path.c_str());
        response["path"] = path;
        int err = fs->remove(path);
        if (err == 0) {
            response["removed"] = true;
        } else {
            response["error"] = std::to_string(err);
        }
    });
}

void registerHttpUpdateCommand(const std::shared_ptr<MqttRoot>& mqttRoot, const std::shared_ptr<FileSystem>& fs) {
    mqttRoot->registerCommand("update", [fs](const JsonObject& request, JsonObject& response) {
        if (!request["url"].is<std::string>()) {
            response["failure"] = "Command contains no URL";
            return;
        }
        std::string url = request["url"];
        if (url.empty()) {
            response["failure"] = "Command contains empty url";
            return;
        }
        HttpUpdater::startUpdate(url, fs);
        response["success"] = true;
    });
}

void initTelemetryPublishTask(
    std::chrono::milliseconds publishInterval,
    const std::shared_ptr<Watchdog>& watchdog,
    const std::shared_ptr<MqttRoot>& mqttRoot,
    const std::shared_ptr<BatteryManager>& batteryManager,
    const std::shared_ptr<PowerManager>& powerManager,
    const std::shared_ptr<WiFiDriver>& wifi,
    const std::shared_ptr<TelemetryCollector>& telemetryCollector,
    const std::shared_ptr<CopyQueue<bool>>& telemetryPublishQueue) {
    Task::loop("telemetry", 8192, [publishInterval, watchdog, mqttRoot, batteryManager, powerManager, wifi, telemetryCollector, telemetryPublishQueue](Task& task) {
        task.markWakeTime();

        mqttRoot->publish("telemetry", [batteryManager, powerManager, wifi, telemetryCollector](JsonObject& telemetry) {
            telemetry["uptime"] = duration_cast<milliseconds>(boot_clock::now().time_since_epoch()).count();
            telemetry["timestamp"] = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

            if (batteryManager != nullptr) {
                auto battery = telemetry["battery"].to<JsonObject>();
                battery["voltage"] = static_cast<double>(batteryManager->getVoltage()) / 1000.0; // Convert to volts
                battery["percentage"] = batteryManager->getPercentage();
            }

            auto wifiData = telemetry["wifi"].to<JsonObject>();
            wifi->populateTelemetry(wifiData);

#if defined(FARMHUB_DEBUG) || defined(FARMHUB_REPORT_MEMORY)
            auto memoryData = telemetry["memory"].to<JsonObject>();
            memoryData["free-heap"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            memoryData["min-heap"] = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
#endif

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

template <std::derived_from<DeviceSettings> TDeviceSettings, std::derived_from<DeviceDefinition<TDeviceSettings>> TDeviceDefinition>
static void startDevice() {
    auto i2c = std::make_shared<I2CManager>();
    auto battery = initBattery<TDeviceDefinition>(i2c);

    initNvsFlash();

    // Install GPIO ISR service
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

#ifdef CONFIG_HEAP_TRACING
    ESP_ERROR_CHECK(heap_trace_init_standalone(trace_record, NUM_RECORDS));
#endif

    auto watchdog = initWatchdog();

    auto deviceDefinition = std::make_shared<TDeviceDefinition>();

    auto fs = std::make_shared<FileSystem>();

    auto settings = loadConfig<TDeviceSettings>(fs, "/device-config.json");

    auto powerManager = std::make_shared<PowerManager>(settings->sleepWhenIdle.get());

    auto logRecords = std::make_shared<Queue<LogRecord>>("logs",
#ifdef FARMHUB_DEBUG
        128
#else
        32
#endif
    );
    ConsoleProvider::init(logRecords, settings->publishLogs.get());

    LOGD("\n"
         "   ______                   _    _       _\n"
         "  |  ____|                 | |  | |     | |\n"
         "  | |__ __ _ _ __ _ __ ___ | |__| |_   _| |__\n"
         "  |  __/ _` | '__| '_ ` _ \\|  __  | | | | '_ \\\n"
         "  | | | (_| | |  | | | | | | |  | | |_| | |_) |\n"
         "  |_|  \\__,_|_|  |_| |_| |_|_|  |_|\\__,_|_.__/ %s\n",
        farmhubVersion);
    LOGI("Initializing FarmHub kernel version %s on %s instance '%s' with hostname '%s' and MAC address %s",
        farmhubVersion,
        settings->model.get().c_str(),
        settings->instance.get().c_str(),
        settings->getHostname().c_str(),
        getMacAddress().c_str());

    auto statusLed = std::make_shared<LedDriver>("status", deviceDefinition->statusPin);
    auto states = std::make_shared<ModuleStates>();
    KernelStatusTask::init(statusLed, states);

    // Init WiFi
    auto wifi = std::make_shared<WiFiDriver>(
        states->networkConnecting,
        states->networkReady,
        states->configPortalRunning,
        settings->getHostname());

    auto telemetryPublishQueue = std::make_shared<CopyQueue<bool>>("telemetry-publish", 1);
    auto telemetryPublisher = std::make_shared<TelemetryPublisher>(telemetryPublishQueue);

    // Init switch and button handling
    auto switches = std::make_shared<SwitchManager>();
    switches->onReleased("factory-reset", deviceDefinition->bootPin, SwitchMode::PullUp, [statusLed, telemetryPublisher](const std::shared_ptr<Switch>&, milliseconds duration) {
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
    });

    // Init battery management
    auto shutdownManager = std::make_shared<ShutdownManager>();
    std::shared_ptr<BatteryManager> batteryManager;
    if (battery != nullptr) {
        LOGD("Battery configured");
        batteryManager = std::make_shared<BatteryManager>(battery, shutdownManager);
    } else {
        LOGD("No battery configured");
    }

#ifdef FARMHUB_DEBUG
    new DebugConsole(batteryManager, wifi);
#endif

    // Init mDNS
    auto mdns = std::make_shared<MdnsDriver>(wifi->getNetworkReady(), settings->getHostname(), "ugly-duckling", farmhubVersion, states->mdnsReady);

    // Init real time clock
    auto rtc = std::make_shared<RtcDriver>(wifi->getNetworkReady(), mdns, settings->ntp.get(), states->rtcInSync);

    // Init MQTT connection
    auto mqttConfig = loadConfig<MqttDriver::Config>(fs, "/mqtt-config.json");
    auto mqttRoot = initMqtt(states, mdns, mqttConfig, settings->instance.get(), settings->location.get());
    MqttLog::init(settings->publishLogs.get(), logRecords, mqttRoot);
    registerBasicCommands(mqttRoot);
    registerFileCommands(mqttRoot, fs);

    // Handle any pending HTTP update (will reboot if update was required and was successful)
    registerHttpUpdateCommand(mqttRoot, fs);
    HttpUpdater::performPendingHttpUpdateIfNecessary(fs, wifi, watchdog);

    auto pcnt = std::make_shared<PcntManager>();
    auto pulseCounterManager = std::make_shared<PulseCounterManager>();
    auto pwm = std::make_shared<PwmManager>();
    auto telemetryCollector = std::make_shared<TelemetryCollector>();

    // Init peripherals
    auto peripheralServices = PeripheralServices {
        .i2c = i2c,
        .pcntManager = pcnt,
        .pulseCounterManager = pulseCounterManager,
        .pwmManager = pwm,
        .switches = switches,
        .telemetryPublisher = telemetryPublisher,
    };
    auto peripheralManager = std::make_shared<PeripheralManager>(telemetryCollector, peripheralServices);
    shutdownManager->registerShutdownListener([peripheralManager]() {
        peripheralManager->shutdown();
    });
    deviceDefinition->registerPeripheralFactories(peripheralManager, peripheralServices, settings);

    // Init functions
    auto functionServices = FunctionServices {
        .telemetryPublisher = telemetryPublisher,
        .peripherals = peripheralManager,
    };
    auto functionManager = std::make_shared<FunctionManager>(fs, functionServices, mqttRoot);
    shutdownManager->registerShutdownListener([functionManager]() {
        functionManager->shutdown();
    });
    deviceDefinition->registerFunctionFactories(functionManager);

    // Init telemetry
    mqttRoot->registerCommand("ping", [telemetryPublisher](const JsonObject&, JsonObject& response) {
        telemetryPublisher->requestTelemetryPublishing();
        response["pong"] = duration_cast<milliseconds>(boot_clock::now().time_since_epoch()).count();
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

    auto& peripheralsSettings = settings->peripherals.get();
    LOGI("Loading configuration for %d user-configured peripherals",
        peripheralsSettings.size());
    for (auto& peripheralSettings : peripheralsSettings) {
        if (!peripheralManager->createPeripheral(peripheralSettings.get(), peripheralsInitJson)) {
            initState = InitState::PeripheralError;
        }
    }

    JsonDocument functionsInitDoc;
    auto functionsInitJson = functionsInitDoc.to<JsonArray>();
    auto& functionsSettings = settings->functions.get();
    LOGI("Loading configuration for %d user-configured functions",
        functionsSettings.size());
    for (auto& functionSettings : functionsSettings) {
        if (!functionManager->createFunction(functionSettings.get(), functionsInitJson)) {
            initState = InitState::FunctionError;
        }
    }

    initTelemetryPublishTask(settings->publishInterval.get(), watchdog, mqttRoot, batteryManager, powerManager, wifi, telemetryCollector, telemetryPublishQueue);

    // Enable power saving once we are done initializing
    WiFiDriver::setPowerSaveMode(settings->sleepWhenIdle.get());

    mqttRoot->publish(
        "init",
        [settings, initState, peripheralsInitJson, functionsInitJson, powerManager](JsonObject& json) {
            // TODO Remove redundant mentions of "ugly-duckling"
            json["type"] = "ugly-duckling";
            json["model"] = settings->model.get();
            json["id"] = settings->id.get();
            json["instance"] = settings->instance.get();
            json["mac"] = getMacAddress();
            auto device = json["settings"].to<JsonObject>();
            settings->store(device);
            // TODO Remove redundant mentions of "ugly-duckling"
            json["app"] = "ugly-duckling";
            json["version"] = farmhubVersion;
            json["reset"] = esp_reset_reason();
            json["wakeup"] = esp_sleep_get_wakeup_cause();
            json["bootCount"] = bootCount++;
            json["time"] = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
            json["state"] = static_cast<int>(initState);
            json["peripherals"].to<JsonArray>().set(peripheralsInitJson);
            json["functions"].to<JsonArray>().set(functionsInitJson);
            json["sleepWhenIdle"] = powerManager->sleepWhenIdle;

            CrashManager::handleCrashReport(json);
        },
        Retention::NoRetain, QoS::AtLeastOnce, 5s);

    states->kernelReady.set();

    LOGI("Device ready in %.2f s (kernel version %s on %s instance '%s' with hostname '%s' and IP '%s', SSID '%s', current time is %lld)",
        duration_cast<milliseconds>(boot_clock::now().time_since_epoch()).count() / 1000.0,
        farmhubVersion,
        settings->model.get().c_str(),
        settings->instance.get().c_str(),
        settings->getHostname().c_str(),
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
