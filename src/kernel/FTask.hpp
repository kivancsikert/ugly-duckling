#pragma once

#include <chrono>
#include <functional>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <Arduino.h>

using namespace std::chrono;

namespace farmhub { namespace kernel {

// TODO Remove _2 suffix
static const uint32_t DEFAULT_STACK_SIZE_2 = 8192;
static const unsigned int DEFAULT_PRIORITY_2 = 1;

class FTask;

typedef std::function<void(FTask&)> TaskFunction;

class FTask {
public:
    static void runTask(const char* name, TaskFunction runFunction) {
        runTask(name, DEFAULT_STACK_SIZE_2, DEFAULT_PRIORITY_2, runFunction);
    }
    static void runTask(const char* name, uint32_t stackSize, UBaseType_t priority, TaskFunction runFunction) {
        FTask* task = new FTask(runFunction);
        Serial.println("Creating task " + String(name) + " with priority " + String(priority) + " and stack size " + String(stackSize) + ".");
        xTaskCreate(executeTask, name, stackSize, task, priority, &(task->taskHandle));
    }

    static void loopTask(const char* name, TaskFunction loopFunction) {
        loopTask(name, DEFAULT_STACK_SIZE_2, DEFAULT_PRIORITY_2, loopFunction);
    }
    static void loopTask(const char* name, uint32_t stackSize, UBaseType_t priority, TaskFunction loopFunction) {
        runTask(name, stackSize, priority, [loopFunction](FTask& task) {
            while (true) {
                loopFunction(task);
            }
        });
    }

    void delay(milliseconds ms) {
        vTaskDelay(pdMS_TO_TICKS(ms.count()));
    }

    void delayUntil(milliseconds ms) {
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(ms.count()));
    }

    void suspend() {
        vTaskSuspend(taskHandle);
    }

    void resume() {
        vTaskResume(taskHandle);
    }

    bool abortDelay() {
        return xTaskAbortDelay(taskHandle);
    }

private:
    FTask(TaskFunction taskFunction)
        : taskFunction(taskFunction)
        , lastWakeTime(xTaskGetTickCount()) {
        Serial.println("Creating FTask with this pointing to " + String((uint32_t) this) + ".");
    }

    static void executeTask(void* parameters) {
        FTask* task = static_cast<FTask*>(parameters);
        Serial.println("Got FTask pointing to " + String((uint32_t) task) + ".");
        task->taskFunction(*task);
        vTaskDelete(task->taskHandle);
        delete task;
    }

    TaskFunction taskFunction;
    TaskHandle_t taskHandle;
    TickType_t lastWakeTime;
};

}}    // namespace farmhub::kernel
