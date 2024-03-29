#pragma once

#include <chrono>
#include <functional>

#include <FreeRTOS.h>
#include <task.h>

#include <kernel/Log.hpp>
#include <kernel/Time.hpp>

using namespace std::chrono;

namespace farmhub::kernel {

static const uint32_t DEFAULT_STACK_SIZE = configMINIMAL_STACK_SIZE;
static const unsigned int DEFAULT_PRIORITY = 1;

class Task;

typedef std::function<void(Task&)> TaskFunction;

class TaskHandle {
public:
    TaskHandle(const TaskHandle_t handle)
        : handle(handle) {
    }

    TaskHandle()
        : TaskHandle(nullptr) {
    }

    TaskHandle(const TaskHandle& other)
        : TaskHandle(other.handle) {
    }

    constexpr bool isValid() const {
        return handle != nullptr;
    }

    void suspend() {
        if (isValid()) {
            vTaskSuspend(handle);
        }
    }

    void resume() {
        if (isValid()) {
            vTaskResume(handle);
        }
    }

    bool abortDelay() {
        if (isValid()) {
            return xTaskAbortDelay(handle);
        }
    }

    void abort() {
        if (isValid()) {
            vTaskDelete(handle);
        }
    }

private:
    TaskHandle_t handle;
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
        LOG_TRACE("Creating task %s with priority %d and stack size %d",
            name.c_str(), priority, stackSize);
        TaskHandle_t handle = nullptr;
        auto result = xTaskCreate(executeTask, name.c_str(), stackSize, taskFunction, priority, &handle);
        if (result != pdPASS) {
            LOG_ERROR("Failed to create task %s: %d", name.c_str(), result);
            delete taskFunction;
            return TaskHandle();
        }
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
        LOG_IMMEDIATE("Task '%s' missed deadline by %lld ms\n",
            pcTaskGetName(nullptr), duration_cast<milliseconds>(ticks(newWakeTime - lastWakeTime)).count());
        lastWakeTime = newWakeTime;
        return false;
    }

    /**
     * @brief Ticks to wait until the given `time` since last task wake time.
     *
     * @param time The time period the caller wants elapsed since the last wake time.
     * @return ticks The number of ticks to delay until the given `time` has elapsed since the last wake time,
     *     or zero if the time has already elapsed.
     */
    ticks ticksUntil(ticks time) {
        auto currentTime = ticks(xTaskGetTickCount());
        // Handling tick overflow. If 'currentTime' is less than 'lastWakeTime',
        // it means the tick count has rolled over.
        if (currentTime - ticks(lastWakeTime) < time) {
            // This means 'targetTime' is still in the future, taking into account possible overflow.
            return time - (currentTime - ticks(lastWakeTime));
        } else {
            // 'currentTime' has surpassed our target time, indicating the delay has expired.
            LOG_IMMEDIATE("Task '%s' missed deadline by %lld ms\n",
                pcTaskGetName(nullptr), duration_cast<milliseconds>(currentTime - ticks(lastWakeTime)).count());
            return ticks::zero();
        }
    }

    void suspend() {
        vTaskSuspend(nullptr);
    }

    void yield() {
        taskYIELD();
    }

#ifdef FARMHUB_DEBUG
    static const BaseType_t CONSOLE_BUFFER_INDEX = 1;

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
        LOG_IMMEDIATE("Finished task '%s'",
            pcTaskGetName(nullptr));
#ifdef FARMHUB_DEBUG
        String* buffer = static_cast<String*>(pvTaskGetThreadLocalStoragePointer(nullptr, CONSOLE_BUFFER_INDEX));
        if (buffer != nullptr) {
            delete buffer;
        }
#endif
        vTaskDelete(nullptr);
    }

    static void executeTask(void* parameters) {
        TaskFunction* taskFunctionParam = static_cast<TaskFunction*>(parameters);
        TaskFunction taskFunction(*taskFunctionParam);
        delete taskFunctionParam;

        Task task;
        taskFunction(task);
    }

    TickType_t lastWakeTime { xTaskGetTickCount() };
};

}    // namespace farmhub::kernel
