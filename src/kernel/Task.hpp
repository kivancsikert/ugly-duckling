#pragma once

#include <chrono>
#include <functional>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <Arduino.h>

#include <ArduinoLog.h>

using namespace std::chrono;

namespace farmhub { namespace kernel {

using ticks = std::chrono::duration<uint32_t, std::ratio<1, configTICK_RATE_HZ>>;

static const uint32_t DEFAULT_STACK_SIZE = 2048;
static const unsigned int DEFAULT_PRIORITY = 1;

class Task;

typedef std::function<void(Task&)> TaskFunction;

class Task {
public:
    static void inline run(const String& name, const TaskFunction runFunction) {
        Task::run(name, DEFAULT_STACK_SIZE, DEFAULT_PRIORITY, runFunction);
    }
    static void inline run(const String& name, uint32_t stackSize, const TaskFunction runFunction) {
        Task::run(name, stackSize, DEFAULT_PRIORITY, runFunction);
    }
    static void run(const String& name, uint32_t stackSize, UBaseType_t priority, const TaskFunction runFunction) {
        Task* task = new Task(String(name), runFunction);
        Log.traceln("Creating task %s with priority %d and stack size %d",
            name.c_str(), priority, stackSize);
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
        Serial.printf("Task '%s' missed deadline by %lld ms\n",
            name.c_str(), duration_cast<milliseconds>(ticks(newWakeTime - lastWakeTime)).count());
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

#ifdef FARMHUB_DEBUG
    static const uint32_t CONSOLE_BUFFER_INDEX = 1;

    static String* consoleBuffer() {
        String* buffer = static_cast<String*>(pvTaskGetThreadLocalStoragePointer(nullptr, CONSOLE_BUFFER_INDEX));
        if (buffer == nullptr) {
            buffer = new String();
            vTaskSetThreadLocalStoragePointer(nullptr, CONSOLE_BUFFER_INDEX, static_cast<void*>(buffer));
        }
        return buffer;
    }
#endif

private:
    Task(String name, TaskFunction taskFunction)
        : name(name)
        , taskFunction(taskFunction)
        , lastWakeTime(xTaskGetTickCount()) {
    }

    ~Task() {
#ifdef FARMHUB_DEBUG
        String* buffer = static_cast<String*>(pvTaskGetThreadLocalStoragePointer(taskHandle, CONSOLE_BUFFER_INDEX));
        if (buffer != nullptr) {
            delete buffer;
        }
#endif
        vTaskDelete(taskHandle);
    }

    static void executeTask(void* parameters) {
        Task* task = static_cast<Task*>(parameters);
        Serial.printf("Starting task %s\n",
            task->name.c_str());
        task->taskFunction(*task);
        Serial.printf("Finished task %s\n",
            task->name.c_str());
        delete task;
    }

    const String name;
    const TaskFunction taskFunction;
    TaskHandle_t taskHandle;
    TickType_t lastWakeTime;
};

}}    // namespace farmhub::kernel
