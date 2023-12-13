#pragma once

#include <memory>

namespace farmhub { namespace kernel {

// Polyfill for std::make_unique
template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

}}    // namespace farmhub::kernel
