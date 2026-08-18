#pragma once

// In unit tests, shadow kernel's Log.hpp with the fake implementation
// so that headers using LOGGING_TAG() can be included without ESP-IDF.
#include <FakeLog.hpp>
