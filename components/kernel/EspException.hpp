#pragma once

#include <esp_err.h>
#include <exception>

namespace farmhub::kernel {

class EspException
    : public std::runtime_error {
public:
    explicit EspException(esp_err_t err)
        : std::runtime_error(esp_err_to_name(err)) {
    }
};

#define ESP_ERROR_THROW(err)           \
    do {                               \
        esp_err_t _err_ = (err);       \
        if (_err_ != ESP_OK) {         \
            throw EspException(_err_); \
        }                              \
    } while (0)

}    // namespace farmhub::kernel
