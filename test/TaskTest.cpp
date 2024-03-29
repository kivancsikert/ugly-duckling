#include <gtest/gtest.h>

#include <string>

#include <kernel/Task.hpp>

namespace farmhub::kernel {

class TaskTest : public ::testing::Test {
};

TEST_F(TaskTest, can_create_a_task) {
    int counter = 0;
    Task::run("test", [&](Task& task) {
        counter++;
    });

    LOG_INFO("Waiting for the task to finish");
    vTaskDelay(pdMS_TO_TICKS(10));
    LOG_INFO("Task finished, hopefully");
}

}    // namespace farmhub::kernel
