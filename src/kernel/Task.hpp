#pragma once

#include <chrono>
#include <functional>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <Arduino.h>

using namespace std::chrono;

namespace farmhub { namespace kernel {

using ticks = std::chrono::duration<uint32_t, std::ratio<1, configTICK_RATE_HZ>>;

// TODO Remove _2 suffix
static const uint32_t DEFAULT_STACK_SIZE = 2048;
static const unsigned int DEFAULT_PRIORITY = 1;

class Task;

typedef std::function<void(Task&)> TaskFunction;

class Task {
public:
    static void inline run(const String& name, TaskFunction runFunction) {
        Task::run(name, DEFAULT_STACK_SIZE, DEFAULT_PRIORITY, runFunction);
    }
    static void inline run(const String& name, uint32_t stackSize, TaskFunction runFunction) {
        Task::run(name, stackSize, DEFAULT_PRIORITY, runFunction);
    }
    static void run(const String& name, uint32_t stackSize, UBaseType_t priority, TaskFunction runFunction) {
        Task* task = new Task(String(name), runFunction);
        Serial.println("Creating task " + String(name) + " with priority " + String(priority) + " and stack size " + String(stackSize));
        xTaskCreate(executeTask, name.c_str(), stackSize, task, priority, &(task->taskHandle));
    }

    static void inline loop(const String& name, TaskFunction loopFunction) {
        Task::loop(name, DEFAULT_STACK_SIZE, DEFAULT_PRIORITY, loopFunction);
    }
    static void inline loop(const String& name, uint32_t stackSize, TaskFunction loopFunction) {
        Task::loop(name, stackSize, DEFAULT_PRIORITY, loopFunction);
    }
    static void loop(const String& name, uint32_t stackSize, UBaseType_t priority, TaskFunction loopFunction) {
        Task::run(name, stackSize, priority, [loopFunction](Task& task) {
            while (true) {
                loopFunction(task);
            }
        });
    }

    void delay(ticks time) {
        vTaskDelay(time.count());
    }

    bool delayUntil(ticks time) {
        if (xTaskDelayUntil(&lastWakeTime, time.count())) {
            return true;
        }
        auto newWakeTime = xTaskGetTickCount();
        Serial.println("Task " + name + " missed deadline by " + String(duration_cast<milliseconds>(ticks(newWakeTime - lastWakeTime)).count()) + " ms");
        lastWakeTime = newWakeTime;
        return false;
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
    Task(String name, TaskFunction taskFunction)
        : name(name)
        , taskFunction(taskFunction)
        , lastWakeTime(xTaskGetTickCount()) {
    }

    static void executeTask(void* parameters) {
        Task* task = static_cast<Task*>(parameters);
        task->taskFunction(*task);
        auto handle = task->taskHandle;
        auto name = task->name;
        delete task;
        Serial.println("Finished task " + name);
        vTaskDelete(handle);
    }

    String name;
    TaskFunction taskFunction;
    TaskHandle_t taskHandle;
    TickType_t lastWakeTime;
};

}}    // namespace farmhub::kernel
