#pragma once

#include <chrono>
#include <memory>
#include <utility>

#include <Configuration.hpp>
#include <Log.hpp>
#include <Named.hpp>
#include <functions/Function.hpp>
#include <mqtt/MqttDriver.hpp>
#include <peripherals/api/IFlowMeter.hpp>
#include <peripherals/api/IValve.hpp>
#include <utils/scheduling/OverrideScheduler.hpp>
#include <utils/scheduling/TimeBasedScheduler.hpp>

using namespace std::chrono;
using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;
using namespace farmhub::peripherals::api;
using namespace farmhub::utils::scheduling;

namespace farmhub::functions::plot_controller {

class PlotConfig : public ConfigurationSection {
public:
    ArrayProperty<TimeBasedSchedule> schedule { this, "schedule" };
    Property<std::string> overrideState { this, "overrideState" };
    Property<time_point<system_clock>> overrideUntil { this, "overrideUntil" };

    std::optional<TargetState> getOverrideState() const {
        auto state = overrideState.get();
        if (state == "open") {
            return TargetState::OPEN;
        }
        if (state == "closed") {
            return TargetState::CLOSED;
        }
        return {};
    }
};

class PlotController final
    : public Named,
      public HasConfig<PlotConfig> {
public:
    PlotController(
        const std::string& name,
        const std::shared_ptr<IValve>& valve,
        const std::shared_ptr<IFlowMeter>& flowMeter,
        const std::shared_ptr<TelemetryPublisher>& telemetryPublisher)
        : Named(name)
        , valve(valve)
        , flowMeter(flowMeter)
        , telemetryPublisher(telemetryPublisher) {
        LOGD("Creating plot controller '%s' with valve '%s' and flow meter '%s'",
            name.c_str(), valve->getName().c_str(), flowMeter->getName().c_str());

        Task::run(name, 4096, [this, name](Task& /*task*/) {
            auto shouldPublishTelemetry = true;
            while (true) {
                ScheduleResult result;
                auto overrideResult = overrideScheduler.tick();
                shouldPublishTelemetry |= overrideResult.shouldPublishTelemetry;

                auto timeBasedResult = timeBasedScheduler.tick();
                shouldPublishTelemetry |= timeBasedResult.shouldPublishTelemetry;

                if (overrideResult.targetState.has_value()) {
                    result = overrideResult;
                } else {
                    result = timeBasedResult;
                }

                auto nextDeadline = clamp(result.nextDeadline.has_value()
                        ? *(result.nextDeadline)
                        : 1s);

                auto transitionHappened = this->valve->transitionTo(result.targetState);
                if (transitionHappened) {
                    LOGI("Plot controller '%s' transitioned to state %d, will evaluate again after %lld ms",
                        name.c_str(),
                        farmhub::peripherals::api::toString(result.targetState),
                        duration_cast<milliseconds>(nextDeadline).count());
                } else {
                    LOGD("Plot controller '%s' stayed in state %d, will evaluate again after %lld ms",
                        name.c_str(),
                        farmhub::peripherals::api::toString(result.targetState),
                        duration_cast<milliseconds>(nextDeadline).count());
                }
                shouldPublishTelemetry |= transitionHappened;

                if (shouldPublishTelemetry) {
                    this->telemetryPublisher->requestTelemetryPublishing();
                    shouldPublishTelemetry = false;
                }

                // TODO Account for time spent in transitionTo()
                updateQueue.pollIn(nextDeadline, [this, &shouldPublishTelemetry](const ConfigSpec& config) {
                    overrideScheduler.setOverride(config.overrideSpec);
                    timeBasedScheduler.setSchedules(config.schedules);
                    shouldPublishTelemetry = true;
                });
            }
        });
    }

    void configure(const std::shared_ptr<PlotConfig>& config) override {
        auto overrideState = config->getOverrideState();
        auto overrideSpec = overrideState.has_value()
            ? std::make_optional<OverrideSchedule>({ *overrideState, config->overrideUntil.get() })
            : std::nullopt;
        updateQueue.put(ConfigSpec { overrideSpec, config->schedule.get() });
    }

private:
    struct ConfigSpec {
        std::optional<OverrideSchedule> overrideSpec;
        std::list<TimeBasedSchedule> schedules;
    };

    std::shared_ptr<IValve> valve;
    std::shared_ptr<IFlowMeter> flowMeter;
    std::shared_ptr<TelemetryPublisher> telemetryPublisher;

    OverrideScheduler overrideScheduler;
    TimeBasedScheduler timeBasedScheduler;

    Queue<ConfigSpec> updateQueue { "updateQueue", 1 };
};

class PlotSettings
    : public ConfigurationSection {
public:
    Property<std::string> valve { this, "valve" };
    Property<std::string> flowMeter { this, "flowMeter" };
};

inline FunctionFactory makeFactory() {
    return makeFunctionFactory<PlotController, PlotSettings, PlotConfig>(
        "plot-controller",
        [](const FunctionInitParameters& params, const std::shared_ptr<PlotSettings>& settings) {
            auto valve = params.peripheral<IValve>(settings->valve.get());
            auto flowMeter = params.peripheral<IFlowMeter>(settings->flowMeter.get());
            return std::make_shared<PlotController>(params.name, valve, flowMeter, params.services.telemetryPublisher);
        });
}

}    // namespace farmhub::functions::plot_controller
