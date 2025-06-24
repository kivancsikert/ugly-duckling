#include <Device.hpp>

#if defined(MK4)
#include <devices/UglyDucklingMk4.hpp>
using Definition = farmhub::devices::UglyDucklingMk4;
using Settings = farmhub::devices::Mk4Settings;
#elif defined(MK5)
#include <devices/UglyDucklingMk5.hpp>
using Definition = farmhub::devices::UglyDucklingMk5;
using Settings = farmhub::devices::Mk5Settings;
#elif defined(MK6)
#include <devices/UglyDucklingMk6.hpp>
using Definition = farmhub::devices::UglyDucklingMk6;
using Settings = farmhub::devices::Mk6Settings;
#elif defined(MK7)
#include <devices/UglyDucklingMk7.hpp>
using Definition = farmhub::devices::UglyDucklingMk7;
using Settings = farmhub::devices::Mk7Settings;
#elif defined(MK8)
#include <devices/UglyDucklingMk8.hpp>
using Definition = farmhub::devices::UglyDucklingMk8;
using Settings = farmhub::devices::Mk8Settings;
#else
#error "No device defined"
#endif

extern "C" void app_main() {
    startDevice<Definition, Settings>();
}
