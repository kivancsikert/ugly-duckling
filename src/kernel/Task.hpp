#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// TODO Use a logger instead of Serial
#include <Arduino.h>

namespace farmhub { namespace kernel {

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
        setup();

        while (true) {
            loop();
        }
    }

    virtual void setup() {
    }

    virtual void loop() = 0;
};

class IntermittentLoopTask : public LoopTask {
public:
    IntermittentLoopTask(const char* name, uint32_t stackSize = DEFAULT_STACK_SIZE, unsigned int priority = DEFAULT_PRIORITY)
        : LoopTask(name, stackSize, priority) {
    }

protected:
    void loop() override {
        auto interval = loopAndDelay();
        delayUntil(interval);
    }

    virtual int loopAndDelay() = 0;
};

}}