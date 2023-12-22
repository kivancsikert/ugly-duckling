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

class TaskHandle {
public:
    TaskHandle(const TaskHandle_t handle)
        : handle(handle) {
    }

    TaskHandle(const TaskHandle& other)
        : handle(other.handle) {
    }

    void suspend() {
        vTaskSuspend(handle);
    }

    void resume() {
        vTaskResume(handle);
    }

    bool abortDelay() {
        return xTaskAbortDelay(handle);
    }

    const TaskHandle_t handle;
};

class Task {
public:
    static TaskHandle inline run(const String& name, const TaskFunction runFunction) {
        return Task::run(name, DEFAULT_STACK_SIZE, DEFAULT_PRIORITY, runFunction);
    }
    static TaskHandle inline run(const String& name, uint32_t stackSize, const TaskFunction runFunction) {
        return Task::run(name, stackSize, DEFAULT_PRIORITY, runFunction);
    }
    static TaskHandle run(const String& name, uint32_t stackSize, UBaseType_t priority, const TaskFunction runFunction) {
        TaskFunction* taskFunction = new TaskFunction(runFunction);
        Log.traceln("Creating task %s with priority %d and stack size %d",
            name.c_str(), priority, stackSize);
        TaskHandle_t handle = nullptr;
        xTaskCreate(executeTask, name.c_str(), stackSize, taskFunction, priority, &handle);
        return TaskHandle(handle);
    }

    static TaskHandle inline loop(const String& name, TaskFunction loopFunction) {
        return Task::loop(name, DEFAULT_STACK_SIZE, DEFAULT_PRIORITY, loopFunction);
    }
    static TaskHandle inline loop(const String& name, uint32_t stackSize, TaskFunction loopFunction) {
        return Task::loop(name, stackSize, DEFAULT_PRIORITY, loopFunction);
    }
    static TaskHandle loop(const String& name, uint32_t stackSize, UBaseType_t priority, TaskFunction loopFunction) {
        return Task::run(name, stackSize, priority, [loopFunction](Task& task) {
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
            pcTaskGetName(nullptr), duration_cast<milliseconds>(ticks(newWakeTime - lastWakeTime)).count());
        lastWakeTime = newWakeTime;
        return false;
    }

    void suspend() {
        vTaskSuspend(nullptr);
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
    ~Task() {
#ifdef FARMHUB_DEBUG
        String* buffer = static_cast<String*>(pvTaskGetThreadLocalStoragePointer(nullptr, CONSOLE_BUFFER_INDEX));
        if (buffer != nullptr) {
            delete buffer;
        }
#endif
        vTaskDelete(nullptr);
    }

    static void executeTask(void* parameters) {
        TaskFunction* taskFunction = static_cast<TaskFunction*>(parameters);
        Log.traceln("Starting task %s\n",
            pcTaskGetName(nullptr));
        Task task;
        (*taskFunction)(task);
        Log.traceln("Finished task %s\n",
            pcTaskGetName(nullptr));
        delete taskFunction;
    }

    TickType_t lastWakeTime { xTaskGetTickCount() };
};

}}    // namespace farmhub::kernel
