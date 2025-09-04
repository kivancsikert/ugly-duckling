#pragma once

#include <chrono>
#include <memory>
#include <utility>

#include <BootClock.hpp>
#include <Configuration.hpp>
#include <Log.hpp>
#include <Named.hpp>
#include <functions/Function.hpp>
#include <mqtt/MqttDriver.hpp>
#include <peripherals/api/IFlowMeter.hpp>
#include <peripherals/api/ISoilMoistureSensor.hpp>
#include <peripherals/api/IValve.hpp>
#include <utils/Chrono.hpp>
#include <utils/scheduling/CompositeScheduler.hpp>
#include <utils/scheduling/MoistureBasedScheduler.hpp>
#include <utils/scheduling/OverrideScheduler.hpp>
#include <utils/scheduling/TimeBasedScheduler.hpp>

using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;
using namespace farmhub::peripherals::api;
using namespace farmhub::utils::scheduling;

namespace farmhub::functions::plot_controller {

struct SoilMoistureTarget : ConfigurationSection {
    Property<Percent> low { this, "low", 60.0 };
    Property<Percent> high { this, "high", 80.0 };
};

struct PlotConfig : ConfigurationSection {
    ArrayProperty<TimeBasedSchedule> schedule { this, "schedule" };
    NamedConfigurationEntry<SoilMoistureTarget> soilMoistureTarget { this, "soilMoistureTarget" };
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

struct BootClock {
    static ms now() {
        return duration_cast<ms>(boot_clock::now().time_since_epoch());
    }
};

enum Mode : uint8_t {
    OVERRIDE,
    SCHEDULE,
    SOIL_MOISTURE_TARGET,
};

class PlotController final
    : public Named,
      public HasConfig<PlotConfig> {
public:
    PlotController(
        const std::string& name,
        const std::shared_ptr<IValve>& valve,
        std::shared_ptr<OverrideScheduler> overrideScheduler,
        std::shared_ptr<TimeBasedScheduler> timeBasedScheduler,
        std::shared_ptr<MoistureBasedScheduler<BootClock>> moistureBasedScheduler,
        const std::shared_ptr<TelemetryPublisher>& telemetryPublisher)
        : Named(name) {
        LOGD("Creating plot controller '%s' with valve '%s'",
            name.c_str(),
            valve->getName().c_str());

        auto compositeScheduler = std::make_shared<CompositeScheduler>(std::list<std::shared_ptr<IScheduler>>{
            overrideScheduler,
            timeBasedScheduler,
            moistureBasedScheduler
        });

        Task::run(name, 4096, [this, name, valve, compositeScheduler, overrideScheduler, timeBasedScheduler, moistureBasedScheduler, telemetryPublisher](Task& /*task*/) {
            auto shouldPublishTelemetry = true;
            while (true) {
                ScheduleResult result = compositeScheduler->tick();
                shouldPublishTelemetry |= result.shouldPublishTelemetry;

                auto nextDeadline = clampTicks(result.nextDeadline.value_or(ms::max()));

                auto transitionHappened = valve->transitionTo(result.targetState);
                if (transitionHappened) {
                    LOGI("Plot controller '%s' transitioned to state %s, will evaluate again after %lld ms",
                        name.c_str(),
                        farmhub::peripherals::api::toString(result.targetState),
                        duration_cast<milliseconds>(nextDeadline).count());
                } else {
                    LOGD("Plot controller '%s' stayed in state %s, will evaluate again after %lld ms",
                        name.c_str(),
                        farmhub::peripherals::api::toString(result.targetState),
                        duration_cast<milliseconds>(nextDeadline).count());
                }
                shouldPublishTelemetry |= transitionHappened;

                if (shouldPublishTelemetry) {
                    telemetryPublisher->requestTelemetryPublishing();
                    shouldPublishTelemetry = false;
                }

                // TODO Account for time spent in transitionTo()
                configQueue.pollIn(nextDeadline, [&](const ConfigSpec& config) {
                    overrideScheduler->setOverride(config.overrideSpec);
                    timeBasedScheduler->setSchedules(config.scheduleSpec);
                    moistureBasedScheduler->setTarget(config.soilMoistureTargetSpec);
                    shouldPublishTelemetry = true;
                });
            }
        });
    }

    void configure(const std::shared_ptr<PlotConfig>& config) override {
        auto overrideState = config->getOverrideState();
        auto overrideSpec = overrideState.has_value()
            ? std::make_optional<OverrideSchedule>({
                  .state = *overrideState,
                  .until = config->overrideUntil.get(),
              })
            : std::nullopt;
        auto soilMoistureTargetSpec = config->soilMoistureTarget.hasValue()
            ? std::make_optional<MoistureTarget>({
                  .low = config->soilMoistureTarget.get()->low.get(),
                  .high = config->soilMoistureTarget.get()->high.get(),
              })
            : std::nullopt;
        configQueue.put(ConfigSpec {
            .overrideSpec = overrideSpec,
            .scheduleSpec = config->schedule.get(),
            .soilMoistureTargetSpec = soilMoistureTargetSpec,
        });
    }

private:
    struct ConfigSpec {
        std::optional<OverrideSchedule> overrideSpec;
        std::list<TimeBasedSchedule> scheduleSpec;
        std::optional<MoistureTarget> soilMoistureTargetSpec;
    };
    Queue<ConfigSpec> configQueue { "configQueue", 1 };
};

class PlotSettings
    : public ConfigurationSection {
public:
    Property<std::string> valve { this, "valve" };
    Property<std::string> flowMeter { this, "flowMeter" };
    Property<std::string> soilMoistureSensor { this, "soilMoistureSensor" };
};

struct NoOpFlowMeter : virtual IFlowMeter, Named {
    explicit NoOpFlowMeter(const std::string& name) : Named(name) {}

    Liters getVolume() override {
        return 0;
    }

    const std::string& getName() const override {
        return Named::name;
    }
};

struct NoOpSoilMoistureSensor : virtual ISoilMoistureSensor, Named {
    explicit NoOpSoilMoistureSensor(const std::string& name) : Named(name) {}

    Percent getMoisture() override {
        return 0;
    }

    const std::string& getName() const override {
        return Named::name;
    }
};

inline FunctionFactory makeFactory() {
    return makeFunctionFactory<PlotController, PlotSettings, PlotConfig>(
        "plot-controller",
        [](const FunctionInitParameters& params, const std::shared_ptr<PlotSettings>& settings) {
            auto valve = params.peripheral<IValve>(settings->valve.get());
            auto flowMeter = settings->flowMeter.hasValue()
                ? params.peripheral<IFlowMeter>(settings->flowMeter.get())
                : std::make_shared<NoOpFlowMeter>(params.name + ":flow");
            auto soilMoistureSensor = settings->soilMoistureSensor.hasValue()
                ? params.peripheral<ISoilMoistureSensor>(settings->soilMoistureSensor.get())
                : std::make_shared<NoOpSoilMoistureSensor>(params.name + ":soil");
            return std::make_shared<PlotController>(
                params.name,
                valve,
                std::make_shared<OverrideScheduler>(),
                std::make_shared<TimeBasedScheduler>(),
                std::make_shared<MoistureBasedScheduler<BootClock>>(
                    Config {},
                    std::make_shared<BootClock>(),
                    flowMeter,
                    soilMoistureSensor),
                params.services.telemetryPublisher);
        });
}

}    // namespace farmhub::functions::plot_controller
