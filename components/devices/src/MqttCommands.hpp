#pragma once

#include <NvsStore.hpp>
#include <drivers/LedDriver.hpp>
#include <mqtt/MqttRoot.hpp>

#include <memory>

using namespace cornucopia::ugly_duckling::kernel;
using namespace cornucopia::ugly_duckling::kernel::mqtt;

void registerBasicCommands(const std::shared_ptr<MqttRoot>& mqttRoot);
void registerNvsCommands(const std::shared_ptr<MqttRoot>& mqttRoot);
void registerHttpUpdateCommand(const std::shared_ptr<MqttRoot>& mqttRoot, const std::shared_ptr<NvsStore>& nvs);
