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
#include <peripherals/valve/ValveConfig.hpp>

using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;
using namespace farmhub::peripherals::flow_meter;
using namespace farmhub::peripherals::valve;

namespace farmhub::functions::plot_controller {

class PlotConfig : public ValveConfig {
};

class PlotController
    : public Named,
      public HasConfig<PlotConfig>,
      public HasShutdown {
public:
    PlotController(
        const std::string& name,
        const std::shared_ptr<Valve>& valve,
        const std::shared_ptr<FlowMeter>& flowMeter)
        : Named(name)
        , valve(valve)
        , flowMeter(flowMeter) {
    }

    void configure(const std::shared_ptr<PlotConfig>& config) override {
        valve->configure(config->schedule.get(), config->overrideState.get(), config->overrideUntil.get());
    }

    void shutdown(const ShutdownParameters& /*parameters*/) override {
        valve->closeBeforeShutdown();
    }

private:
    std::shared_ptr<Valve> valve;
    std::shared_ptr<FlowMeter> flowMeter;
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
