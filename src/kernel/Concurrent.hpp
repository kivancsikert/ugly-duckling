#pragma once

#include <chrono>
#include <functional>
#include <utility>

#include <freertos/FreeRTOS.h>

#include <kernel/Task.hpp>

using namespace std::chrono;

namespace farmhub::kernel {

template <typename TMessage>
class Queue {
public:
    Queue(const String& name, size_t capacity = 16)
        : name(name)
        , queue(xQueueCreate(capacity, sizeof(TMessage*))) {
    }

    ~Queue() {
        vQueueDelete(queue);
    }

    typedef std::function<void(const TMessage&)> MessageHandler;

    template <typename... Args>
    void put(Args&&... args) {
        while (!offerIn(ticks::max(), std::forward<Args>(args)...)) { }
    }

    template <typename... Args>
    bool offer(Args&&... args) {
        return offerIn(ticks::zero(), std::forward<Args>(args)...);
    }

    template <typename... Args>
    bool offerIn(ticks timeout, Args&&... args) {
        TMessage* copy = new TMessage(std::forward<Args>(args)...);
        bool sentWithoutDropping = xQueueSend(queue, &copy, timeout.count()) == pdTRUE;
        if (!sentWithoutDropping) {
            Serial.printf("Overflow in queue '%s', dropping message\n",
                name.c_str());
            delete copy;
        }
        return sentWithoutDropping;
    }

    template <typename... Args>
    bool offerFromISR(Args&&... args) {
        TMessage* copy = new TMessage(std::forward<Args>(args)...);
        BaseType_t xHigherPriorityTaskWoken;
        bool sentWithoutDropping = xQueueSendFromISR(queue, &copy, &xHigherPriorityTaskWoken) == pdTRUE;
        if (!sentWithoutDropping) {
            Serial.printf("Overflow in queue '%s', dropping message\n",
                name.c_str());
            delete copy;
        }
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        return sentWithoutDropping;
    }

    template <typename... Args>
    void overwrite(Args&&... args) {
        TMessage* copy = new TMessage(std::forward<Args>(args)...);
        xQueueOverwrite(queue, &copy);
    }

    template <typename... Args>
    void overwriteFromISR(Args&&... args) {
        TMessage* copy = new TMessage(std::forward<Args>(args)...);
        BaseType_t xHigherPriorityTaskWoken;
        xQueueOverwriteFromISR(queue, &copy, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    size_t drain(MessageHandler handler) {
        return drain(SIZE_MAX, handler);
    }

    size_t drain(size_t maxItems, MessageHandler handler) {
        size_t count = 0;
        while (count < maxItems) {
            if (!poll(handler)) {
                break;
            }
            count++;
        }
        return count;
    }

    void take() {
        take([](const TMessage& message) {});
    }

    void take(MessageHandler handler) {
        while (!pollIn(ticks::max(), handler)) { }
    }

    bool poll() {
        return poll([](const TMessage& message) {});
    }

    bool poll(MessageHandler handler) {
        return pollIn(ticks::zero(), handler);
    }

    bool pollIn(ticks timeout) {
        return pollIn(timeout, [](const TMessage& message) {});
    }

    bool pollIn(ticks timeout, MessageHandler handler) {
        TMessage* message;
        if (!xQueueReceive(queue, &message, timeout.count())) {
            return false;
        }
        handler(*message);
        delete message;
        return true;
    }

    void clear() {
        xQueueReset(queue);
    }

private:
    const String name;
    const QueueHandle_t queue;
};

class Mutex {
public:
    Mutex()
        : mutex(xSemaphoreCreateMutex()) {
    }

    ~Mutex() {
        vSemaphoreDelete(mutex);
    }

    void lock() {
        while (!lockIn(ticks::max())) { }
    }

    bool tryLock() {
        return lockIn(ticks::zero());
    }

    bool lockIn(ticks timeout) {
        return xSemaphoreTake(mutex, timeout.count());
    }

    void unlock() {
        xSemaphoreGive(mutex);
    }

private:
    const String name;
    const SemaphoreHandle_t mutex;
};

class  Lock {
public:
    Lock(Mutex& mutex)
        : mutex(mutex) {
        mutex.lock();
    }

    ~Lock() {
        mutex.unlock();
    }

    // Delete copy constructor and assignment operator to prevent copying
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

private:
    Mutex& mutex;
};

}    // namespace farmhub::kernel
