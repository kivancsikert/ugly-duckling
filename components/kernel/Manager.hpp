#pragma once

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>

#include <Concurrent.hpp>

namespace farmhub::kernel {

// Generic, TU-stable type tokens: address of per-type inline variable
template <typename T>
inline constexpr char TypeTokenVar = 0;

// A reusable, shutdown-agnostic, type-erased handle that keeps a shared_ptr to an implementation
// and provides tryGet<T>() via a compile-time type token. Lifecycle operations like shutdown are
// intentionally kept out of this generic type and should be orchestrated by domain managers.
class Handle {
public:
    Handle() = default;

    template <typename ImplPtr>
    static Handle wrap(const ImplPtr& impl) {
        Handle h;
        using Impl = std::remove_reference_t<decltype(*impl)>;
        // Store the impl as void and record a per-type token
        h._holder = std::static_pointer_cast<void>(impl);
        h._typeTag = &TypeTokenVar<Impl>;
        return h;
    }

    // Typed access without RTTI
    template <typename T>
    std::shared_ptr<T> tryGet() const {
        if (_typeTag == &TypeTokenVar<T>) {
            return std::static_pointer_cast<T>(_holder);
        }
        return {};
    }

private:
    std::shared_ptr<void> _holder;
    const void* _typeTag { nullptr };
};

// A lightweight, generic factory descriptor. The CreateFn is the concrete callable type
// (often a std::function) that returns a Product from domain-specific parameters.
template <typename Product, typename CreateFn>
struct Factory {
    std::string factoryType;    // key used for registration
    std::string productType;    // human-readable/type-identifying string
    CreateFn create;            // callable to create Product
};

template <typename Product, typename FactoryT>
class Manager {
public:
    explicit Manager(std::string managed)
        : managed(std::move(managed)) {
    }

    void registerFactory(FactoryT factory) {
        LOGD("Registering %s factory: %s", managed.c_str(), factory.factoryType.c_str());
        factories.emplace(factory.factoryType, std::move(factory));
    }

protected:
    template <typename T>
    std::shared_ptr<T> getInstance(const std::string& name) const {
        Lock lock(mutex);
        auto it = instances.find(name);
        if (it != instances.end()) {
            return it->second.template tryGet<T>();
        }
        return {};
    }

    void createWithFactory(const std::string& name, const std::string& type, const std::function<Product(const FactoryT&)>& make) {
        Lock lock(mutex);
        LOGD("Creating peripheral '%s' with factory '%s'",
            name.c_str(), type.c_str());
        auto it = factories.find(type);
        if (it == factories.end()) {
            throw std::runtime_error("Factory not found");
        }
        const auto& factory = it->second;
        Product instance = make(factory);
        instances.emplace(name, std::move(instance));
    }

    Mutex& getMutex() {
        return mutex;
    }

private:
    std::string managed;
    std::map<std::string, FactoryT> factories;
    mutable Mutex mutex;
    std::unordered_map<std::string, Product> instances;
};

}    // namespace farmhub::kernel
