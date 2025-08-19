#pragma once

#include "Drv8833Driver.hpp"

namespace farmhub::kernel::drivers {

/**
 * @brief Texas Instruments DRV8848 dual motor driver.
 *
 * It's basically the same as the DRV8833.
 *
 * https://www.ti.com/lit/gpn/DRV8848
 */
using Drv8848Driver = Drv8833Driver;

} // namespace farmhub::kernel::drivers
