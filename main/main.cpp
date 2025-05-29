#include <Device.hpp>

using namespace farmhub::devices;

#if defined(MK4)
#include <devices/UglyDucklingMk4.hpp>
using Definition = UglyDucklingMk4;
using Config = Mk4Config;
#elif defined(MK5)
#include <devices/UglyDucklingMk5.hpp>
using Definition = UglyDucklingMk5;
using Config = Mk5Config;
#elif defined(MK6)
#include <devices/UglyDucklingMk6.hpp>
using Definition = UglyDucklingMk6;
using Config = Mk6Config;
#elif defined(MK7)
#include <devices/UglyDucklingMk7.hpp>
using Definition = UglyDucklingMk7;
using Config = Mk7Config;
#elif defined(MK8)
#include <devices/UglyDucklingMk8.hpp>
using Definition = UglyDucklingMk8;
using Config = Mk8Config;
#else
#error "No device defined"
#endif

extern "C" void app_main() {
    startDevice<Definition, Config>();
}
