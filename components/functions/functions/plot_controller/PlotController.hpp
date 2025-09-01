#pragma once

#include <memory>
#include <utility>

#include <Configuration.hpp>
#include <Named.hpp>
#include <functions/Function.hpp>
#include <mqtt/MqttDriver.hpp>
#include <peripherals/Motors.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/flow_meter/FlowMeter.hpp>
#include <peripherals/valve/Valve.hpp>
#include <utils/scheduling/OverrideScheduler.hpp>
#include <utils/scheduling/TimeBasedScheduler.hpp>

using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;
using namespace farmhub::peripherals::flow_meter;
using namespace farmhub::peripherals::valve;
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
        const std::shared_ptr<Valve>& valve,
        const std::shared_ptr<FlowMeter>& flowMeter)
        : Named(name)
        , valve(valve)
        , flowMeter(flowMeter) {
        LOGD("Creating plot controller '%s' with valve '%s' and flow meter '%s'",
            name.c_str(), valve->name.c_str(), flowMeter->name.c_str());
    }

    void configure(const std::shared_ptr<PlotConfig>& config) override {
        auto overrideState = config->getOverrideState();
        if (overrideState.has_value()) {
            overrideScheduler.setOverride(*overrideState, config->overrideUntil.get());
        } else {
            overrideScheduler.clear();
        }
        timeBasedScheduler.setSchedules(config->schedule.get());
    }

private:
    std::shared_ptr<Valve> valve;
    std::shared_ptr<FlowMeter> flowMeter;

    OverrideScheduler overrideScheduler;
    TimeBasedScheduler timeBasedScheduler;
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
            auto valve = params.peripherals->getInstance<Valve>(settings->valve.get());
            auto flowMeter = params.peripherals->getInstance<FlowMeter>(settings->flowMeter.get());
            return std::make_shared<PlotController>(params.name, valve, flowMeter);
        });
}

}    // namespace farmhub::functions::plot_controller

namespace ArduinoJson {

using farmhub::utils::scheduling::TimeBasedSchedule;

template <>
struct Converter<TimeBasedSchedule> {
    static void toJson(const TimeBasedSchedule& src, JsonVariant dst) {
        JsonObject obj = dst.to<JsonObject>();
        obj["start"] = src.start;
        obj["period"] = src.period.count();
        obj["duration"] = src.duration.count();
    }

    static TimeBasedSchedule fromJson(JsonVariantConst src) {
        auto start = src["start"].as<time_point<system_clock>>();
        auto period = seconds(src["period"].as<int64_t>());
        auto duration = seconds(src["duration"].as<int64_t>());
        return { .start = start, .period = period, .duration = duration };
    }

    static bool checkJson(JsonVariantConst src) {
        return src["start"].is<time_point<system_clock>>()
            && src["period"].is<int64_t>()
            && src["duration"].is<int64_t>();
    }
};

}    // namespace ArduinoJson
