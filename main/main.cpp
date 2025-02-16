#if defined(MK4)
#include <devices/UglyDucklingMk4.hpp>
typedef farmhub::devices::UglyDucklingMk4 Definition;
typedef farmhub::devices::Mk4Config Config;
#elif defined(MK5)
#include <devices/UglyDucklingMk5.hpp>
typedef farmhub::devices::UglyDucklingMk5 Definition;
typedef farmhub::devices::Mk5Config Config;
#elif defined(MK6)
#include <devices/UglyDucklingMk6.hpp>
typedef farmhub::devices::UglyDucklingMk6 Definition;
typedef farmhub::devices::Mk6Config Config;
#elif defined(MK7)
#include <devices/UglyDucklingMk7.hpp>
typedef farmhub::devices::UglyDucklingMk7 Definition;
typedef farmhub::devices::Mk7Config Config;
#elif defined(MK8)
#include <devices/UglyDucklingMk8.hpp>
typedef farmhub::devices::UglyDucklingMk8 Definition;
typedef farmhub::devices::Mk8Config Config;
#else
#error "No device defined"
#endif

#include <Device.hpp>

extern "C" void app_main() {
    startDevice<Definition, Config>();
}
