#include <catch2/catch_test_macros.hpp>

#include <Strings.hpp>

TEST_CASE("toHexString") {
    REQUIRE(farmhub::kernel::toHexString(0) == "0");
    REQUIRE(farmhub::kernel::toHexString(1) =="1");
    REQUIRE(farmhub::kernel::toHexString(15) == "f");
    REQUIRE(farmhub::kernel::toHexString(0x123456ab) == "123456ab");
}
