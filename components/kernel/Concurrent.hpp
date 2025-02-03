#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <memory>

#include <freertos/FreeRTOS.h>

#include <Time.hpp>
#include <BootClock.hpp>

using namespace std::chrono;

namespace farmhub::kernel {

class BaseQueue {
protected:
    BaseQueue(const std::string& name, size_t messageSize, size_t capacity)
        : name(name)
        , queue(xQueueCreate(capacity, messageSize)) {
    }

    ~BaseQueue() {
        vQueueDelete(queue);
    }

public:
    virtual void clear() = 0;

    UBaseType_t size() {
        return uxQueueMessagesWaiting(queue);
    }

protected:

    const std::string name;
    const QueueHandle_t queue;
};

template <typename TMessage>
class Queue : public BaseQueue {
public:
    Queue(const std::string& name, size_t capacity = 16)
        : BaseQueue(name, sizeof(TMessage*), capacity) {
    }

    template <typename... Args>
    requires std::constructible_from<TMessage, Args...>
    void put(Args&&... args) {
        while (!offerIn(ticks::max(), std::forward<Args>(args)...)) { }
    }

    template <typename... Args>
    requires std::constructible_from<TMessage, Args...>
    bool offer(Args&&... args) {
        return offerIn(ticks::zero(), std::forward<Args>(args)...);
    }

    template <typename... Args>
    requires std::constructible_from<TMessage, Args...>
    bool offerIn(ticks timeout, Args&&... args) {
        TMessage* copy = new TMessage(std::forward<Args>(args)...);
        bool sentWithoutDropping = xQueueSend(this->queue, &copy, timeout.count()) == pdTRUE;
        if (!sentWithoutDropping) {
            printf("Overflow in queue '%s', dropping message\n",
                this->name.c_str());
            delete copy;
        }
        return sentWithoutDropping;
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

    /**
     * @brief Wait for the first item to appear within the given timeout,
     * then drain any items remaining in the queue.
     */
    size_t drainIn(ticks timeout, MessageHandler handler) {
        return drainIn(SIZE_MAX, timeout, handler);
    }

    /**
     * @brief Wait for the first item to appear within the given timeout,
     * then drain no more than `maxItems` items remaining in the queue.
     */
    size_t drainIn(size_t maxItems, ticks timeout, MessageHandler handler) {
        size_t count = 0;
        ticks nextTimeout = timeout;
        while (true) {
            if (count >= maxItems) {
                break;
            }
            if (!pollIn(nextTimeout, handler)) {
                break;
            }
            count++;
            nextTimeout = ticks::zero();
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
    CopyQueue(const std::string& name, size_t capacity = 16)
        : BaseQueue(name, sizeof(TMessage), capacity) {
    }

    void put(const TMessage message) {
        while (!offerIn(ticks::max(), message)) { }
    }

    bool offer(const TMessage message) {
        return offerIn(ticks::zero(), message);
    }

    bool offerIn(ticks timeout, const TMessage message) {
        bool sentWithoutDropping = xQueueSend(this->queue, &message, timeout.count()) == pdTRUE;
        if (!sentWithoutDropping) {
            printf("Overflow in queue '%s', dropping message",
                this->name.c_str());
        }
        return sentWithoutDropping;
    }

    bool IRAM_ATTR offerFromISR(const TMessage& message) {
        BaseType_t xHigherPriorityTaskWoken;
        bool sentWithoutDropping = xQueueSendFromISR(this->queue, &message, &xHigherPriorityTaskWoken) == pdTRUE;
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        return sentWithoutDropping;
    }

    void overwrite(const TMessage message) {
        xQueueOverwrite(this->queue, &message);
    }

    void IRAM_ATTR overwriteFromISR(const TMessage& message) {
        BaseType_t xHigherPriorityTaskWoken;
        xQueueOverwriteFromISR(this->queue, &message, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    TMessage take() {
        while (true) {
            auto message = pollIn(ticks::max());
            if (message.has_value()) {
                return message.value();
            }
        }
    }

    std::optional<TMessage> poll() {
        return pollIn(ticks::zero());
    }

    std::optional<TMessage> pollIn(ticks timeout) {
        TMessage message;
        if (xQueueReceive(this->queue, &message, timeout.count())) {
            return message;
        }
        return std::nullopt;
    }

    void clear() {
        xQueueReset(this->queue);
    }
};

class MutexBase {
public:
    void lock() {
        while (!lockIn(ticks::max())) { }
    }

    bool tryLock() {
        return lockIn(ticks::zero());
    }

    virtual bool lockIn(ticks timeout) = 0;

    virtual void unlock() = 0;
};

class Mutex : public MutexBase {
public:
    Mutex()
        : mutex(xSemaphoreCreateMutex()) {
    }

    ~Mutex() {
        vSemaphoreDelete(mutex);
    }

    bool lockIn(ticks timeout) override {
        return xSemaphoreTake(mutex, timeout.count());
    }

    void unlock() override {
        xSemaphoreGive(mutex);
    }

private:
    const SemaphoreHandle_t mutex;
};

class RecursiveMutex : public MutexBase {
public:
    RecursiveMutex()
        : mutex(xSemaphoreCreateRecursiveMutex()) {
    }

    ~RecursiveMutex() {
        vSemaphoreDelete(mutex);
    }

    bool lockIn(ticks timeout) override {
        return xSemaphoreTakeRecursive(mutex, timeout.count());
    }

    void unlock() override {
        xSemaphoreGiveRecursive(mutex);
    }

private:
    const SemaphoreHandle_t mutex;
};

class  Lock {
public:
    Lock(MutexBase& mutex)
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
    MutexBase& mutex;
};

}    // namespace farmhub::kernel
