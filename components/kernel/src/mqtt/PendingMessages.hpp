#pragma once

#include <memory>
#include <unordered_map>

#include <Concurrent.hpp>

namespace cornucopia::ugly_duckling::kernel::mqtt {

enum class PublishStatus : uint8_t {
    TimeOut = 0,
    Success = 1,
    Failed = 2,
    Pending = 3,
    QueueFull = 4
};

/**
 * @brief One-shot outcome slot for a single publish, identified by nothing but its own identity
 * (there's exactly one of these per in-flight `publishAndWait` call). Resolved at most once, by
 * whichever side gets there first: the real MQTT outcome (ack/fail/disconnect-cleanup), or the
 * caller giving up locally. A caller that stops waiting (timeout) simply drops its reference;
 * if the real outcome arrives later it resolves an object nobody's listening to anymore instead
 * of racing a stale notification onto some *other*, unrelated wait -- there's no shared channel
 * left to cross-talk over.
 *
 * Lifetime is shared between `PendingMessages` (which holds it by message ID until the outcome
 * arrives or the connection drops) and the waiting call stack, via `shared_ptr`: whichever side
 * lets go last is the one that frees it, so it's never leaked and never double-resolved.
 */
class PendingMessage {
public:
    PendingMessage()
        : outcome("mqtt-pending-message", 1) {
    }

    PendingMessage(const PendingMessage&) = delete;
    PendingMessage& operator=(const PendingMessage&) = delete;

    /**
     * @brief Resolves the outcome. Must be called at most once (a second call would find the
     * one-slot queue already full and just print an overflow warning instead of blocking).
     */
    void resolve(PublishStatus status) {
        outcome.offer(status);
    }

    /**
     * @brief Blocks the calling task until `resolve()` is called or `timeout` elapses.
     */
    PublishStatus await(ticks timeout) {
        return outcome.pollIn(timeout).value_or(PublishStatus::TimeOut);
    }

private:
    CopyQueue<PublishStatus> outcome;
};

using PendingMessagePtr = std::shared_ptr<PendingMessage>;

class PendingMessages {
public:
    /**
     * @brief Registers `pending` as awaiting the outcome of `messageId`, unless it's resolved
     * immediately: `pending` may be null (fire-and-forget publish, nobody is waiting), and
     * QoS 0 messages never get a PUBLISHED/DELETED event from esp-mqtt, so they're resolved as
     * successful right away instead of being tracked.
     */
    void waitOn(int messageId, const PendingMessagePtr& pending) {
        if (pending == nullptr) {
            return;
        }

        if (messageId == 0) {
            pending->resolve(PublishStatus::Success);
            return;
        }

        Lock lock(mutex);
        messages[messageId] = pending;
    }

    bool handlePublished(int messageId, bool success) {
        if (messageId == 0) {
            return false;
        }

        PendingMessagePtr pending;
        {
            Lock lock(mutex);
            auto it = messages.find(messageId);
            if (it == messages.end()) {
                return false;
            }
            pending = std::move(it->second);
            messages.erase(it);
        }
        pending->resolve(success ? PublishStatus::Success : PublishStatus::Failed);
        return true;
    }

    void clear() {
        std::unordered_map<int, PendingMessagePtr> abandoned;
        {
            Lock lock(mutex);
            abandoned = std::move(messages);
            messages.clear();
        }
        for (auto& [messageId, pending] : abandoned) {
            pending->resolve(PublishStatus::Failed);
        }
    }

private:
    Mutex mutex;
    std::unordered_map<int, PendingMessagePtr> messages;
};

}    // namespace cornucopia::ugly_duckling::kernel::mqtt
