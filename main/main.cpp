#include <Device.hpp>

#if defined(MK4)
#include <devices/UglyDucklingMk4.hpp>
using Definition = farmhub::devices::UglyDucklingMk4;
using Config = farmhub::devices::Mk4Config;
#elif defined(MK5)
#include <devices/UglyDucklingMk5.hpp>
using Definition = farmhub::devices::UglyDucklingMk5;
using Config = farmhub::devices::Mk5Config;
#elif defined(MK6)
#include <devices/UglyDucklingMk6.hpp>
using Definition = farmhub::devices::UglyDucklingMk6;
using Config = farmhub::devices::Mk6Config;
#elif defined(MK7)
#include <devices/UglyDucklingMk7.hpp>
using Definition = farmhub::devices::UglyDucklingMk7;
using Config = farmhub::devices::Mk7Config;
#elif defined(MK8)
#include <devices/UglyDucklingMk8.hpp>
using Definition = farmhub::devices::UglyDucklingMk8;
using Config = farmhub::devices::Mk8Config;
#else
#error "No device defined"
#endif

extern "C" void app_main() {
    startDevice<Definition, Config>();
}
