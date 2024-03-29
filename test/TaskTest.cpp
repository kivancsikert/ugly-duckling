#include <gtest/gtest.h>

#include <string>

#include <kernel/Task.hpp>

namespace farmhub::kernel {

class TaskTest : public ::testing::Test {
};

TEST_F(TaskTest, can_create_a_task) {
    Task::run("test", 1024, [](Task& task) {
        printf("Task running\n");
    });
}

}    // namespace farmhub::kernel
