#include <unity.h>

#include <Strings.hpp>

TEST_CASE("toHexString works", "[test]") {
    TEST_ASSERT_EQUAL_STRING("0", farmhub::kernel::toHexString(0).c_str());
    TEST_ASSERT_EQUAL_STRING("1", farmhub::kernel::toHexString(1).c_str());
    TEST_ASSERT_EQUAL_STRING("f", farmhub::kernel::toHexString(15).c_str());
    TEST_ASSERT_EQUAL_STRING("123456ab", farmhub::kernel::toHexString(0x123456ab).c_str());
}
