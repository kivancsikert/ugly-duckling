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
#include <kernel/HttpUpdate.hpp>
#include <kernel/Log.hpp>

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

#include <devices/Device.hpp>

extern "C" void app_main() {
    auto i2c = std::make_shared<I2CManager>();
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

    Log::init();

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Install GPIO ISR service
    gpio_install_isr_service(0);

#ifdef CONFIG_HEAP_TRACING
    ESP_ERROR_CHECK(heap_trace_init_standalone(trace_record, NUM_RECORDS));
#endif

    auto fs = FileSystem::get();

    auto deviceConfig = std::make_shared<TDeviceConfiguration>();
    // TODO This should just be a "load()" call
    ConfigurationFile<TDeviceConfiguration> deviceConfigFile(fs, "/device-config.json", deviceConfig);

    auto logRecords = std::make_shared<Queue<LogRecord>>("logs", 32);
    ConsoleProvider::init(logRecords, deviceConfig->publishLogs.get());

    LOGD("   ______                   _    _       _");
    LOGD("  |  ____|                 | |  | |     | |");
    LOGD("  | |__ __ _ _ __ _ __ ___ | |__| |_   _| |__");
    LOGD("  |  __/ _` | '__| '_ ` _ \\|  __  | | | | '_ \\");
    LOGD("  | | | (_| | |  | | | | | | |  | | |_| | |_) |");
    LOGD("  |_|  \\__,_|_|  |_| |_| |_|_|  |_|\\__,_|_.__/ %s", farmhubVersion);
    LOGD("  ");

    StateManager stateManager;
    StateSource networkConnectingState = stateManager.createStateSource("network-connecting");
    StateSource networkReadyState = stateManager.createStateSource("network-ready");
    StateSource configPortalRunningState = stateManager.createStateSource("config-portal-running");
    auto wifi = std::make_shared<WiFiDriver>(
        networkConnectingState,
        networkReadyState,
        configPortalRunningState,
        deviceConfig->getHostname());

    auto deviceDefinition = std::make_shared<TDeviceDefinition>(deviceConfig);

    auto statusLed = std::make_shared<LedDriver>("status", deviceDefinition->statusPin);

    auto shutdownManager = std::make_shared<ShutdownManager>();
    std::shared_ptr<BatteryManager> batteryManager;
    if (battery != nullptr) {
        batteryManager = std::make_shared<BatteryManager>(battery, shutdownManager);
    }

    StateSource mdnsReadyState = stateManager.createStateSource("mdns-ready");
    auto mdns = std::make_shared<MdnsDriver>(wifi->getNetworkReady(), deviceConfig->getHostname(), "ugly-duckling", farmhubVersion, mdnsReadyState);

    // Reboots if update is successful
    handleHttpUpdate(fs, wifi);

    // Enable power saving for WiFi if we don't need to do HTTP update
    wifi->setPowerSaveMode(deviceConfig->sleepWhenIdle.get());

    auto mqttConfig = std::make_shared<MqttDriver::Config>();
    // TODO This should just be a "load()" call
    ConfigurationFile<MqttDriver::Config> mqttConfigFile(fs, "/mqtt-config.json", mqttConfig);

    auto kernel = std::make_shared<Kernel>(deviceConfig, mqttConfig, statusLed, shutdownManager, i2c, wifi, mdns);

    new farmhub::devices::Device(deviceConfig, deviceDefinition, batteryManager, kernel);

#ifdef CONFIG_HEAP_TASK_TRACKING
    Task::loop("task-heaps", 4096, [](Task& task) {
        dumpPerTaskHeapInfo();
        Task::delay(ticks(5s));
    });
#endif

    vTaskDelete(NULL);
}
