#include <gtest/gtest.h>

#include <string>

#include <kernel/Task.hpp>

namespace farmhub::kernel {

class TaskTest : public ::testing::Test {
};

TEST_F(TaskTest, can_create_a_task) {
    Task::run("test", [](Task& task) {
        LOG_TRACE("Task running");
        task.delayUntil(400ms);
        LOG_TRACE("Task running");
        task.delayUntil(400s);
    });

    vTaskDelay(pdMS_TO_TICKS(900));
    LOG_INFO("Task finished, hopefully\n");
}

}    // namespace farmhub::kernel
