// Helper macros to convert macro to string
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#include <atomic>
#include <chrono>
#include <string>

#include <esp_app_desc.h>

static const char* const farmhubVersion = esp_app_get_description()->version;

#include <kernel/Log.hpp>

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
    initLogging();

#ifdef CONFIG_HEAP_TRACING
    ESP_ERROR_CHECK(heap_trace_init_standalone(trace_record, NUM_RECORDS));
#endif

    new farmhub::devices::Device();

#ifdef CONFIG_HEAP_TASK_TRACKING
    Task::loop("task-heaps", 4096, [](Task& task) {
        dumpPerTaskHeapInfo();
        Task::delay(ticks(5s));
    });
#endif

    vTaskDelete(NULL);
}
