#include <gtest/gtest.h>

#include <string>

#include <kernel/Task.hpp>

namespace farmhub::kernel {

class TaskTest : public ::testing::Test {
};

TEST_F(TaskTest, can_create_a_task) {
    Task::run("test", 1024, [](Task& task) {
        LOG_TRACE("Task running\n");
        task.delayUntil(400ms);
        LOG_TRACE("Task running\n");
        task.delayUntil(400s);
    });
    vTaskDelay(pdMS_TO_TICKS(1200));
    printf("Task finished, hopefully\n");
}

}    // namespace farmhub::kernel
