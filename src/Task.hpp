#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// TODO Use a logger instead of Serial
#include <Arduino.h>

namespace farmhub { namespace device { namespace drivers {

static const uint32_t DEFAULT_STACK_SIZE = 10000;
static const unsigned int DEFAULT_PRIORITY = 1;

class Task {
public:
    // TODO What's a good stack depth?
    Task(const char* name, uint32_t stackSize = DEFAULT_STACK_SIZE, unsigned int priority = DEFAULT_PRIORITY) {
        xTaskCreate(taskFunction, name, stackSize, this, priority, &taskHandle);
    }

    TaskHandle_t getHandle() {
        return taskHandle;
    }

protected:
    virtual void run() = 0;

    void delay(int ms) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

    void delayUntil(int ms) {
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(ms));
    }

    void suspend() {
        vTaskSuspend(taskHandle);
    }

    void resume() {
        vTaskResume(taskHandle);
    }

private:
    void execute() {
        lastWakeTime = xTaskGetTickCount();
        run();
        vTaskDelete(taskHandle);
    }

    static void taskFunction(void* parameters) {
        static_cast<Task*>(parameters)->execute();
    }

    TaskHandle_t taskHandle;
    TickType_t lastWakeTime;
};

class LoopTask : public Task {
public:
    LoopTask(const char* name, uint32_t stackSize = DEFAULT_STACK_SIZE, unsigned int priority = DEFAULT_PRIORITY)
        : Task(name, stackSize, priority) {
    }

protected:
    void run() override {
        while (true) {
            auto delay = loop();
            delayUntil(delay);
        }
    }

    virtual int loop() = 0;
};

}}}    // namespace farmhub::device::drivers
