#include <atomic>
#include <chrono>

#include <devices/Device.hpp>

#ifdef CONFIG_HEAP_TASK_TRACKING
#include <esp_heap_task_info.h>

#define MAX_TASK_NUM 20     // Max number of per tasks info that it can store
#define MAX_BLOCK_NUM 20    // Max number of per block info that it can store

static size_t s_prepopulated_num = 0;
static heap_task_totals_t s_totals_arr[MAX_TASK_NUM];
static heap_task_block_t s_block_arr[MAX_BLOCK_NUM];

static void dumpPerTaskHeapInfo() {
    heap_task_info_params_t heap_info = { 0 };
    heap_info.caps[0] = MALLOC_CAP_8BIT;    // Gets heap with CAP_8BIT capabilities
    heap_info.mask[0] = MALLOC_CAP_8BIT;
    heap_info.caps[1] = MALLOC_CAP_32BIT;    // Gets heap info with CAP_32BIT capabilities
    heap_info.mask[1] = MALLOC_CAP_32BIT;
    heap_info.tasks = NULL;    // Passing NULL captures heap info for all tasks
    heap_info.num_tasks = 0;
    heap_info.totals = s_totals_arr;    // Gets task wise allocation details
    heap_info.num_totals = &s_prepopulated_num;
    heap_info.max_totals = MAX_TASK_NUM;     // Maximum length of "s_totals_arr"
    heap_info.blocks = s_block_arr;          // Gets block wise allocation details. For each block, gets owner task, address and size
    heap_info.max_blocks = MAX_BLOCK_NUM;    // Maximum length of "s_block_arr"

    heap_caps_get_per_task_info(&heap_info);

    for (int i = 0; i < *heap_info.num_totals; i++) {
        printf("Task: %s -> CAP_8BIT: %d CAP_32BIT: %d\n",
            heap_info.totals[i].task ? pcTaskGetName(heap_info.totals[i].task) : "Pre-Scheduler allocs",
            heap_info.totals[i].size[0],     // Heap size with CAP_8BIT capabilities
            heap_info.totals[i].size[1]);    // Heap size with CAP32_BIT capabilities
    }

    printf("\n\n");
}
#endif

#ifdef CONFIG_HEAP_TRACING
#include "esp_heap_trace.h"

#define NUM_RECORDS 100
static heap_trace_record_t trace_record[NUM_RECORDS];    // This buffer must be in internal RAM
#endif

extern "C" void app_main() {
    initArduino();

#ifdef CONFIG_HEAP_TRACING
    ESP_ERROR_CHECK(heap_trace_init_standalone(trace_record, NUM_RECORDS));
#endif

    new farmhub::devices::Device();

    while (true) {
#ifdef CONFIG_HEAP_TRACING
#ifdef CONFIG_HEAP_TASK_TRACKING
        dumpPerTaskHeapInfo();
#endif

        ESP_ERROR_CHECK(heap_trace_start(HEAP_TRACE_LEAKS));

        vTaskDelay(5000 / portTICK_PERIOD_MS);

        ESP_ERROR_CHECK(heap_trace_stop());
        heap_trace_dump();
#else
        vTaskDelay(portMAX_DELAY);
#endif
    }
}
