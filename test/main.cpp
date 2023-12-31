#include <gtest/gtest.h>

#if defined(ARDUINO)
#include <Arduino.h>

void setup()
{
    // should be the same value as for the `test_speed` option in "platformio.ini"
    // default value is test_speed=115200
    Serial.begin(115200);

    // give the 1-2 seconds to the test runner to connect to the board
    delay(1000);

    ::testing::InitGoogleTest();
    // if you plan to use GMock, replace the line above with
    // ::testing::InitGoogleMock(&argc, argv);
    if (RUN_ALL_TESTS());
}

void loop()
{
    // nothing to be done here.
    delay(100);
}

#else
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    // if you plan to use GMock, replace the line above with
    // ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
