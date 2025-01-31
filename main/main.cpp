// Helper macros to convert macro to string
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include <driver/gpio.h>
#include <esp_app_desc.h>

static const char* const farmhubVersion = esp_app_get_description()->version;

#include <kernel/BatteryManager.hpp>
#include <kernel/Console.hpp>
#include <kernel/DebugConsole.hpp>
#include <kernel/HttpUpdate.hpp>
#include <kernel/KernelStatus.hpp>
#include <kernel/Log.hpp>
#include <kernel/mqtt/MqttLog.hpp>

#include <devices/Device.hpp>

using namespace farmhub::kernel;

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

void performFactoryReset(std::shared_ptr<LedDriver> statusLed, bool completeReset) {
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

std::shared_ptr<BatteryDriver> initBattery(std::shared_ptr<I2CManager> i2c) {
    auto battery = TDeviceDefinition::createBatteryDriver(i2c);
    if (battery != nullptr) {
        // If the battery voltage is below the device's threshold, we should not boot yet.
        // This is to prevent the device from booting and immediately shutting down
        // due to the high current draw of the boot process.
        auto voltage = battery->getVoltage();
        if (voltage != 0.0 && voltage < battery->parameters.bootThreshold) {
            ESP_LOGW("battery", "Battery voltage too low (%.2f V < %.2f), entering deep sleep\n",
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
std::shared_ptr<TConfiguration> loadConfig(std::shared_ptr<FileSystem> fs, const std::string& path) {
    auto config = std::make_shared<TConfiguration>();
    // TODO This should just be a "load()" call
    ConfigurationFile<TConfiguration> configFile(fs, path, config);
    return config;
}

std::shared_ptr<MqttRoot> initMqtt(std::shared_ptr<ModuleStates> states, std::shared_ptr<MdnsDriver> mdns, std::shared_ptr<MqttDriver::Config> mqttConfig, const std::string& instance, const std::string& location) {
    auto mqtt = std::make_shared<MqttDriver>(states->networkReady, mdns, mqttConfig, instance, states->mqttReady);
    return std::make_shared<MqttRoot>(mqtt, (location.empty() ? "" : location + "/") + "devices/ugly-duckling/" + instance);
}

extern "C" void app_main() {
    auto i2c = std::make_shared<I2CManager>();
    auto battery = initBattery(i2c);

    Log::init();

    initNvsFlash();

    // Install GPIO ISR service
    gpio_install_isr_service(0);

#ifdef CONFIG_HEAP_TRACING
    ESP_ERROR_CHECK(heap_trace_init_standalone(trace_record, NUM_RECORDS));
#endif

    auto watchdog = initWatchdog();

    auto fs = std::make_shared<FileSystem>();

    auto deviceConfig = loadConfig<TDeviceConfiguration>(fs, "/device-config.json");

    auto powerManager = std::make_shared<PowerManager>(deviceConfig->sleepWhenIdle.get());

    auto logRecords = std::make_shared<Queue<LogRecord>>("logs", 32);
    ConsoleProvider::init(logRecords, deviceConfig->publishLogs.get());

    LOGD("   ______                   _    _       _");
    LOGD("  |  ____|                 | |  | |     | |");
    LOGD("  | |__ __ _ _ __ _ __ ___ | |__| |_   _| |__");
    LOGD("  |  __/ _` | '__| '_ ` _ \\|  __  | | | | '_ \\");
    LOGD("  | | | (_| | |  | | | | | | |  | | |_| | |_) |");
    LOGD("  |_|  \\__,_|_|  |_| |_| |_|_|  |_|\\__,_|_.__/ %s", farmhubVersion);
    LOGD("  ");
    LOGI("Initializing FarmHub kernel version %s on %s instance '%s' with hostname '%s' and MAC address %s",
        farmhubVersion,
        deviceConfig->model.get().c_str(),
        deviceConfig->instance.get().c_str(),
        deviceConfig->getHostname().c_str(),
        getMacAddress().c_str());

    auto deviceDefinition = std::make_shared<TDeviceDefinition>(deviceConfig);

    auto statusLed = std::make_shared<LedDriver>("status", deviceDefinition->statusPin);
    auto states = std::make_shared<ModuleStates>();
    KernelStatusTask::init(statusLed, states);

    // Init WiFi
    auto wifi = std::make_shared<WiFiDriver>(
        states->networkConnecting,
        states->networkReady,
        states->configPortalRunning,
        deviceConfig->getHostname());

    // Init switch and button handling
    auto switches = std::make_shared<SwitchManager>();
    switches->onReleased("factory-reset", deviceDefinition->bootPin, SwitchMode::PullUp, [statusLed](const Switch&, milliseconds duration) {
        if (duration >= 15s) {
            LOGI("Factory reset triggered after %lld ms", duration.count());
            performFactoryReset(statusLed, true);
        } else if (duration >= 5s) {
            LOGI("WiFi reset triggered after %lld ms", duration.count());
            performFactoryReset(statusLed, false);
        }
    });

    // Init battery management
    auto shutdownManager = std::make_shared<ShutdownManager>();
    std::shared_ptr<BatteryManager> batteryManager;
    if (battery != nullptr) {
        batteryManager = std::make_shared<BatteryManager>(battery, shutdownManager);
    }

#ifdef FARMHUB_DEBUG
    new DebugConsole(batteryManager, wifi);
#endif

    // Init mDNS
    auto mdns = std::make_shared<MdnsDriver>(wifi->getNetworkReady(), deviceConfig->getHostname(), "ugly-duckling", farmhubVersion, states->mdnsReady);

    // Init real time clock
    auto rtc = std::make_shared<RtcDriver>(wifi->getNetworkReady(), mdns, deviceConfig->ntp.get(), states->rtcInSync);

    // Init MQTT connection
    auto mqttConfig = loadConfig<MqttDriver::Config>(fs, "/mqtt-config.json");
    auto mqttRoot = initMqtt(states, mdns, mqttConfig, deviceConfig->instance.get(), deviceConfig->location.get());
    MqttLog::init(deviceConfig->publishLogs.get(), logRecords, mqttRoot);

    // Handle any pending HTTP update (will reboot if update was required and was successful)
    performHttpUpdateIfNecessary(fs, wifi, watchdog);

    // Init peripherals
    auto peripheralManager = std::make_shared<PeripheralManager>(fs, i2c, deviceDefinition->pcnt, deviceDefinition->pulseCounterManager, deviceDefinition->pwm, switches, mqttRoot);
    deviceDefinition->registerPeripheralFactories(peripheralManager);

    new farmhub::devices::Device(deviceConfig, deviceDefinition, fs, wifi, batteryManager, watchdog, powerManager, shutdownManager, mqttRoot, peripheralManager, states->rtcInSync);

    // Enable power saving once we are done initializing
    wifi->setPowerSaveMode(deviceConfig->sleepWhenIdle.get());

    states->kernelReady.set();

    LOGI("Device ready in %.2f s (kernel version %s on %s instance '%s' with hostname '%s' and IP '%s', SSID '%s', current time is %lld)",
        duration_cast<milliseconds>(boot_clock::now().time_since_epoch()).count() / 1000.0,
        farmhubVersion,
        deviceConfig->model.get().c_str(),
        deviceConfig->instance.get().c_str(),
        deviceConfig->getHostname().c_str(),
        wifi->getIp().value_or("<no-ip>").c_str(),
        wifi->getSsid().value_or("<no-ssid>").c_str(),
        duration_cast<seconds>(system_clock::now().time_since_epoch()).count());

#ifdef CONFIG_HEAP_TASK_TRACKING
    Task::loop("task-heaps", 4096, [](Task& task) {
        dumpPerTaskHeapInfo();
        Task::delay(ticks(5s));
    });
#endif

    vTaskDelete(NULL);
}
