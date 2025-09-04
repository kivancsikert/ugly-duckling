#pragma once

#include <list>

#include <utils/Chrono.hpp>

#include "IScheduler.hpp"

namespace farmhub::utils::scheduling {

struct CompositeScheduler : IScheduler {
    explicit CompositeScheduler(std::list<std::shared_ptr<IScheduler>> schedulers)
        : schedulers(std::move(schedulers)) {
    }

    ScheduleResult tick() override {
        ScheduleResult result;
        for (auto& scheduler : schedulers) {
            // Stop ticking lower priority schedulers once we have a decision
            if (result.targetState) {
                // TODO Reset unused schedulers
                break;
            }
            auto subResult = scheduler->tick();
            result = merge(result, subResult);
        }
        return result;
    }

    const char* getName() const override {
        return "composite";
    }

private:
    static ScheduleResult merge(const ScheduleResult& a, const ScheduleResult& b) {
        return {
            .targetState = a.targetState ? a.targetState : b.targetState,
            .nextDeadline = minDuration(a.nextDeadline, b.nextDeadline),
            .shouldPublishTelemetry = a.shouldPublishTelemetry || b.shouldPublishTelemetry
        };
    }

    std::list<std::shared_ptr<IScheduler>> schedulers;
};

}    // namespace farmhub::utils::scheduling
