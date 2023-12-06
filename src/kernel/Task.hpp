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

class Task;

typedef std::function<void(Task&)> TaskFunction;

class Task {
public:
    static void run(const char* name, TaskFunction runFunction) {
        Task::run(name, DEFAULT_STACK_SIZE_2, DEFAULT_PRIORITY_2, runFunction);
    }
    static void run(const char* name, uint32_t stackSize, UBaseType_t priority, TaskFunction runFunction) {
        Task* task = new Task(runFunction);
        Serial.println("Creating task " + String(name) + " with priority " + String(priority) + " and stack size " + String(stackSize) + ".");
        xTaskCreate(executeTask, name, stackSize, task, priority, &(task->taskHandle));
    }

    static void loop(const char* name, TaskFunction loopFunction) {
        Task::loop(name, DEFAULT_STACK_SIZE_2, DEFAULT_PRIORITY_2, loopFunction);
    }
    static void loop(const char* name, uint32_t stackSize, UBaseType_t priority, TaskFunction loopFunction) {
        Task::run(name, stackSize, priority, [loopFunction](Task& task) {
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
    Task(TaskFunction taskFunction)
        : taskFunction(taskFunction)
        , lastWakeTime(xTaskGetTickCount()) {
        Serial.println("Creating Task with this pointing to " + String((uint32_t) this) + ".");
    }

    static void executeTask(void* parameters) {
        Task* task = static_cast<Task*>(parameters);
        Serial.println("Got Task pointing to " + String((uint32_t) task) + ".");
        task->taskFunction(*task);
        vTaskDelete(task->taskHandle);
        delete task;
    }

    TaskFunction taskFunction;
    TaskHandle_t taskHandle;
    TickType_t lastWakeTime;
};

}}    // namespace farmhub::kernel
