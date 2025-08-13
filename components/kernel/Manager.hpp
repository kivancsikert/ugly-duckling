#pragma once

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <type_traits>

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
    static Handle wrap(std::string name, const ImplPtr& impl) {
        Handle h;
        h.name = std::move(name);
        using Impl = std::remove_reference_t<decltype(*impl)>;
        // Store the impl as void and record a per-type token
        h._holder = std::static_pointer_cast<void>(impl);
        h._typeTag = &TypeTokenVar<Impl>;
        return h;
    }

    // Name of the instance (product-specific semantics)
    std::string name;

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
        LOGD("Registering %s factory: %s",
            managed.c_str(),
            factory.factoryType.c_str());
        factories.emplace(factory.factoryType, std::move(factory));
    }

protected:
    // Find a factory by type key; returns nullptr when not found
    const FactoryT* findFactory(const std::string& type) const {
        auto it = factories.find(type);
        if (it == factories.end()) {
            return nullptr;
        }
        return &it->second;
    }

    // Add a created product instance to the managed list
    void addInstance(Product instance) {
        Lock lock(mutex);
        instances.push_back(std::move(instance));
    }

    template <typename T>
    std::shared_ptr<T> getInstance(const std::string& name) {
        Lock lock(mutex);
        for (const auto& inst : instances) {
            if (inst.name == name) {
                if (auto ptr = inst.template tryGet<T>()) {
                    return ptr;
                }
            }
        }
        return {};
    }

    Mutex& getMutex() {
        return mutex;
    }

private:
    std::string managed;
    std::map<std::string, FactoryT> factories;
    Mutex mutex;
    std::list<Product> instances;
};

}    // namespace farmhub::kernel
