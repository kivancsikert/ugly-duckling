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
void invokeRunTests(void* params) {
    int* result = (int*)params;
    printf("Running tests\n");
    *result = runTests();
    vTaskEndScheduler();
}

int main(int argc, char** argv) {
    TaskHandle_t runnerHandle = nullptr;
    int result = -1;
    auto created = xTaskCreate(
        invokeRunTests,
        "gtest:main",
        16 * 1024,
        &result,
        1,
        &runnerHandle);

    if (created != pdPASS) {
        printf("Failed to create the test runner task\n");
        return -1;
    }

    printf("Starting scheduler\n");

    /* Start the tasks and timer running. */
    vTaskStartScheduler();

    return result;
}

// uint8_t ucHeap[configTOTAL_HEAP_SIZE];

void vAssertCalled(const char* const pcFileName, unsigned long ulLine) {
    printf("Assert in %s:%lu\n", pcFileName, ulLine);
}

void vApplicationIdleHook(void) {
}
void vApplicationStackOverflowHook(TaskHandle_t pxTask, char* pcTaskName) {
    printf("Stack overflow in %s\n", pcTaskName);
}
void vApplicationTickHook(void) {
}

#endif
