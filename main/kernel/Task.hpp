#pragma once

#include <chrono>
#include <functional>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <kernel/Log.hpp>
#include <kernel/Time.hpp>

using namespace std::chrono;

namespace farmhub::kernel {

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

    TaskHandle& operator=(const TaskHandle& other) {
        if (this != &other) { // Self-assignment check
            handle = other.handle;
        }
        return *this;
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
    static TaskHandle inline run(const std::string& name, uint32_t stackSize, const TaskFunction runFunction) {
        return Task::run(name, stackSize, DEFAULT_PRIORITY, runFunction);
    }
    static TaskHandle run(const std::string& name, uint32_t stackSize, UBaseType_t priority, const TaskFunction runFunction) {
        TaskFunction* taskFunction = new TaskFunction(runFunction);
        LOGD("Creating task %s with priority %d and stack size %ld",
            name.c_str(), priority, stackSize);
        TaskHandle_t handle = nullptr;
        auto result = xTaskCreate(executeTask, name.c_str(), stackSize, taskFunction, priority, &handle);
        if (result != pdPASS) {
            LOGE("Failed to create task %s: %d", name.c_str(), result);
            delete taskFunction;
            return TaskHandle();
        }
        return TaskHandle(handle);
    }

    enum class RunResult {
        OK,
        TIMEOUT,
    };

    static RunResult inline runIn(const std::string& name, ticks timeout, uint32_t stackSize, const TaskFunction runFunction) {
        return Task::runIn(name, timeout, stackSize, DEFAULT_PRIORITY, runFunction);
    }
    static RunResult runIn(const std::string& name, ticks timeout, uint32_t stackSize, UBaseType_t priority, const TaskFunction runFunction) {
        TaskHandle_t caller = xTaskGetCurrentTaskHandle();
        TaskHandle callee = run(name, stackSize, priority, [runFunction, caller](Task& task) {
            runFunction(task);
            xTaskNotifyGive(caller);
        });
        auto result = xTaskNotifyWait(0, 0, nullptr, timeout.count());
        if (result == pdTRUE) {
            return RunResult::OK;
        } else {
            callee.abort();
            LOGV("Task '%s' timed out",
                name.c_str());
            return RunResult::TIMEOUT;
        }
    }

    static TaskHandle inline loop(const std::string& name, uint32_t stackSize, TaskFunction loopFunction) {
        return Task::loop(name, stackSize, DEFAULT_PRIORITY, loopFunction);
    }
    static TaskHandle loop(const std::string& name, uint32_t stackSize, UBaseType_t priority, TaskFunction loopFunction) {
        return Task::run(name, stackSize, priority, [loopFunction](Task& task) {
            while (true) {
                loopFunction(task);
            }
        });
    }

    static inline void delay(ticks time) {
        // LOGV("Task '%s' delaying for %lld ms",
        //     pcTaskGetName(nullptr), duration_cast<milliseconds>(time).count());
        vTaskDelay(time.count());
    }

    bool delayUntil(ticks time) {
        if (delayUntilAtLeast(time)) {
            return true;
        }
        auto newWakeTime = xTaskGetTickCount();
        printf("Task '%s' missed deadline by %lld ms\n",
            pcTaskGetName(nullptr), duration_cast<milliseconds>(ticks(newWakeTime - lastWakeTime)).count());
        lastWakeTime = newWakeTime;
        return false;
    }

    bool delayUntilAtLeast(ticks time) {
        // LOGV("Task '%s' delaying until %lld ms",
        //     pcTaskGetName(nullptr), duration_cast<milliseconds>(time).count());
        return xTaskDelayUntil(&lastWakeTime, time.count());
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
            // printf("Task '%s' is already past deadline by %lld ms\n",
            //     pcTaskGetName(nullptr), duration_cast<milliseconds>(currentTime - ticks(lastWakeTime)).count());
            return ticks::zero();
        }
    }

    /**
     * @brief Mark the current time as the last wake time.
     */
    void markWakeTime() {
        lastWakeTime = xTaskGetTickCount();
    }

    void suspend() {
        vTaskSuspend(nullptr);
    }

    void yield() {
        taskYIELD();
    }

private:
    ~Task() {
        LOGV("Finished task %s\n",
            pcTaskGetName(nullptr));
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
