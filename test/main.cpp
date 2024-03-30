#include <gtest/gtest.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

int runTests() {
    ::testing::InitGoogleTest();
    // if you plan to use GMock, replace the line above with
    // ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}

#if defined(ARDUINO)
#include <Arduino.h>

void setup() {
    // should be the same value as for the `test_speed` option in "platformio.ini"
    // default value is test_speed=115200
    Serial.begin(115200);

    // give the 1-2 seconds to the test runner to connect to the board
    delay(1000);

    if (runTests())
        ;
}

void loop() {
    // nothing to be done here.
    delay(100);
}

#else
// Taken from https://forums.freertos.org/t/unit-test-strategy/12529

static pthread_t m_freertos_thread_id;
static int m_result = 0;
static bool m_test_is_done = false;

/**
 * FreeRTOS task which runs all my unit tests.
 */
static void gtest_task(void* param) {
    (void) param;
    // Run Google Test from here!
    m_result = RUN_ALL_TESTS();
    m_test_is_done = true;
    // Note: A call to vTaskEndScheduler() never returns.
    vTaskEndScheduler();
}

/**
 * Creates the unit test FreeRTOS task.
 */
static void start_free_rtos() {
    BaseType_t rtos_res = xTaskCreate(gtest_task,
        "gtest",
        configMINIMAL_STACK_SIZE * 10,
        nullptr,
        1,
        nullptr);
    if (rtos_res != pdPASS) {
        abort();
    }
}

static void end_free_rtos() {
    pthread_join(m_freertos_thread_id, nullptr);
}

/**
 * FreeRTOS scheduler thread.
 */
static void* free_rtos_thread(void* data) {
    (void) data;

    // Make this thread cancellable
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    // Start tests
    start_free_rtos();
    // Start FreeRTOS scheduler (it never returns)
    vTaskStartScheduler();
    return nullptr;
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);

    // Mask out all signals to main thread
    {
        sigset_t set;
        sigfillset(&set);
        pthread_sigmask(SIG_SETMASK, &set, NULL);
    }

    // Create the FreeRTOS scheduler thread and wait until it is done
    sigset_t set;
    pthread_t tid;
    pthread_attr_t attr;

    sigfillset(&set);
    pthread_attr_init(&attr);
    pthread_attr_setsigmask_np(&attr, &set);

    pthread_t freeRtosThreadId;
    pthread_create(&freeRtosThreadId, &attr, &free_rtos_thread, nullptr);

    while (!m_test_is_done) {
        sleep(1);
    }

    end_free_rtos();

    return m_result;
}

void vAssertCalled(const char* const pcFileName, unsigned long ulLine) {
    printf("Assert in %s:%lu\n", pcFileName, ulLine);
}

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char* pcTaskName) {
    printf("Stack overflow in %s\n", pcTaskName);
}

#endif
