#pragma once

#include <chrono>
#include <functional>
#include <utility>

#include <freertos/FreeRTOS.h>

#include <kernel/Task.hpp>

using namespace std::chrono;

namespace farmhub::kernel {

class BaseQueue {
protected:
    BaseQueue(const String& name, size_t messageSize, size_t capacity)
        : name(name)
        , queue(xQueueCreate(capacity, messageSize)) {
    }

    ~BaseQueue() {
        vQueueDelete(queue);
    }

public:
    virtual void clear() = 0;

protected:

    const String name;
    const QueueHandle_t queue;
};

template <typename TMessage>
class Queue : public BaseQueue {
public:
    Queue(const String& name, size_t capacity = 16)
        : BaseQueue(name, sizeof(TMessage*), capacity) {
    }

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
        bool sentWithoutDropping = xQueueSend(this->queue, &copy, timeout.count()) == pdTRUE;
        if (!sentWithoutDropping) {
            Serial.printf("Overflow in queue '%s', dropping message\n",
                this->name.c_str());
            delete copy;
        }
        return sentWithoutDropping;
    }

    template <typename... Args>
    void overwrite(Args&&... args) {
        TMessage* copy = new TMessage(std::forward<Args>(args)...);
        xQueueOverwrite(this->queue, &copy);
    }

    typedef std::function<void(TMessage&)> MessageHandler;

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
        if (!xQueueReceive(this->queue, &message, timeout.count())) {
            return false;
        }
        handler(*message);
        delete message;
        return true;
    }

    void clear() override {
        this->drain([](const TMessage& message) {});
    }
};

template <typename TMessage>

class CopyQueue : public BaseQueue {
public:
    CopyQueue(const String& name, size_t capacity = 16)
        : BaseQueue(name, sizeof(TMessage), capacity) {
    }

    bool IRAM_ATTR offerFromISR(const TMessage& message) {
        BaseType_t xHigherPriorityTaskWoken;
        bool sentWithoutDropping = xQueueSendFromISR(this->queue, &message, &xHigherPriorityTaskWoken) == pdTRUE;
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        return sentWithoutDropping;
    }

    void IRAM_ATTR overwriteFromISR(const TMessage& message) {
        BaseType_t xHigherPriorityTaskWoken;
        xQueueOverwriteFromISR(this->queue, &message, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    TMessage take() {
        TMessage message;
        while (!xQueueReceive(this->queue, &message, ticks::max().count())) {
        }
        return message;
    }

    void clear() {
        xQueueReset(this->queue);
    }
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
