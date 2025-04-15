#pragma once

#include <esp_err.h>
#include <exception>

namespace farmhub::kernel {

class EspException
    : public std::runtime_error {
public:
    explicit EspException(const std::string& reason)
        : std::runtime_error(reason) {
    }
};

#define ESP_ERROR_THROW(err)                                           \
    do {                                                               \
        esp_err_t _err_ = (err);                                       \
        if (_err_ != ESP_OK) {                                         \
            throw EspException(esp_err_to_name(_err_)); \
        }                                                              \
    } while (0)

}    // namespace farmhub::kernel
