#pragma once

#include <list>

#include <utils/Chrono.hpp>

#include "IScheduler.hpp"

namespace farmhub::utils::scheduling {

struct CompositeScheduler : IScheduler {
    CompositeScheduler(std::list<std::shared_ptr<IScheduler>> schedulers)
        : schedulers(std::move(schedulers)) {
    }

    ScheduleResult tick() override {
        ScheduleResult result;
        for (auto& scheduler : schedulers) {
            auto subResult = scheduler->tick();
            result = merge(result, subResult);
        }
        return result;
    }

private:

    inline static ScheduleResult merge(const ScheduleResult& a, const ScheduleResult& b) {
        return {
            .targetState = a.targetState ? a.targetState : b.targetState,
            .nextDeadline = minDuration(a.nextDeadline, b.nextDeadline),
            .shouldPublishTelemetry = a.shouldPublishTelemetry || b.shouldPublishTelemetry
        };
    }

    std::list<std::shared_ptr<IScheduler>> schedulers;
};

}    // namespace farmhub::utils::scheduling
