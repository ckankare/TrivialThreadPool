#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <variant>

#if defined(_WIN32) || defined(__CYGWIN__)
#include <malloc.h>
#else
#include <cstdlib>
#endif

namespace ttp {

namespace details {
    inline constexpr void* aligned_alloc(std::size_t size, std::size_t align) noexcept {
#if defined(_WIN32) || defined(__CYGWIN__)
        auto ptr = _aligned_malloc(size, align);
#else
        auto ptr = std::aligned_alloc(align, size);
#endif
        return ptr;
    }

    inline constexpr void aligned_free(void* ptr) noexcept {
#if defined(_WIN32) || defined(__CYGWIN__)
        _aligned_free(ptr);
#else
        free(ptr);
#endif
    }
} // namespace details

// :TODO, allow custom allocator
template <std::size_t N>
class Storage {
public:
    // static_assert(N >= sizeof(void*), "N should be larger than the size of a pointer!");

    using inline_storage_t = std::aligned_storage_t<N, alignof(std::max_align_t)>;
    static constexpr std::size_t inline_alignment = alignof(inline_storage_t);

    constexpr Storage() = default;

    constexpr Storage(std::size_t size, std::size_t alignment) noexcept {
        // :TODO Allow custom allocator. NOTE Might be a bit tricky, as
        // std::allocators can't allocate "raw" data...
        if (size <= N && alignment <= inline_alignment) {
            m_data.template emplace<0>();
        } else {
            auto* ptr = details::aligned_alloc(size, alignment);
            m_data.template emplace<1>(Allocation{ptr, size});
        }
    }

    template <typename T>
    constexpr explicit Storage(T&& data) noexcept : Storage(sizeof(T), alignof(T)) {
        void* ptr = pointer();
        new (ptr) T(std::forward<T>(data));
    }

    template <typename T>
    constexpr explicit Storage(T& data) noexcept : Storage(sizeof(T), alignof(T)) {
        void* ptr = pointer();
        new (ptr) T(std::forward<T>(data));
    }

    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    Storage(Storage&& rhs) noexcept {
        m_data = std::move(rhs.m_data);
        rhs.m_data.template emplace<0>();
    }

    Storage& operator=(Storage&& rhs) noexcept {
        this->~Storage();
        m_data = std::move(rhs.m_data);
        rhs.m_data.template emplace<0>();

        return *this;
    }

    ~Storage() noexcept {
        if (m_data.index() == 1) {
            details::aligned_free(std::get<1>(m_data).ptr);
        }
    }

    bool is_inlined() const { return m_data.index() == 0; }

    void* pointer() noexcept {
        if (m_data.index() == 0) {
            return &std::get<0>(m_data);
        } else {
            return std::get<1>(m_data).ptr;
        }
    }

    const void* pointer() const noexcept {
        if (m_data.index() == 0) {
            return &std::get<0>(m_data);
        } else {
            return std::get<1>(m_data).ptr;
        }
    }

    template <typename T>
    T& cast() noexcept {
        return *reinterpret_cast<T*>(pointer());
    }

    template <typename T>
    const T& cast() const noexcept {
        return *reinterpret_cast<const T*>(pointer());
    }

private:
    struct Allocation {
        void* ptr;
        std::size_t size;
    };
    std::variant<inline_storage_t, Allocation> m_data;
};

namespace details {
    template <typename T>
    static void mover(void* lhs, void* rhs) noexcept {
        new (lhs) T(std::move(*reinterpret_cast<T*>(rhs)));
    }

    template <typename T>
    static void destroyer(void* instance) noexcept {
        reinterpret_cast<T*>(instance)->~T();
    }

    template <typename T, typename R, typename... Args>
    static R caller(void* instance, Args&&... args) {
        T& call = *reinterpret_cast<T*>(instance);
        return std::invoke(call, std::forward<Args>(args)...);
    }
} // namespace details

// :TODO, allow custom allocator
// :TODO, could also have type that allows copied; however, we
// don't have a need for it in ttp.
template <std::size_t N>
class ErasuredType {
public:
    constexpr ErasuredType() : m_valid(false) {}

    template <typename T>
    constexpr explicit ErasuredType(T&& data) noexcept : m_valid(true), m_storage(std::forward<T>(data)) {
        m_mover = &details::mover<std::decay_t<T>>;
        m_destroyer = &details::destroyer<std::decay_t<T>>;
    }

    ErasuredType(const ErasuredType&) = delete;
    ErasuredType& operator=(const ErasuredType&) = delete;

    ErasuredType(ErasuredType&& rhs) noexcept {
        m_valid = false;
        *this = std::move(rhs);
    }

    ErasuredType& operator=(ErasuredType&& rhs) noexcept {
        if (m_valid) {
            m_destroyer(m_storage.pointer());
            m_valid = false;
        }
        if (!rhs.m_valid) {
            return *this;
        }

        if (!rhs.m_storage.is_inlined()) {
            m_storage = std::move(rhs.m_storage);
        } else {
            // :TODO, if trivial and stuff we can just copy or something
            // instead.
            rhs.m_mover(m_storage.pointer(), rhs.m_storage.pointer());
        }
        m_mover = rhs.m_mover;
        m_destroyer = rhs.m_destroyer;

        m_valid = true;
        rhs.m_valid = false;
        return *this;
    }

    ~ErasuredType() noexcept {
        if (m_valid) {
            m_destroyer(m_storage.pointer());
        }
    }

    template <typename T>
    T& cast() noexcept {
        return m_storage.template cast<T>();
    }

    template <typename T>
    const T& cast() const noexcept {
        return m_storage.template cast<T>();
    }

    void* pointer() { return m_storage.pointer(); }

    const void* pointer() const { return m_storage.pointer(); }

    bool valid() const { return m_valid; }

    bool inlined() const { return m_storage.is_inlined(); }

private:
    bool m_valid;
    bool m_trivial;
    Storage<N> m_storage;

    using mover_t = void (*)(void* lhs, void* rhs);
    using destroyer_t = void (*)(void* instance);

    mover_t m_mover;
    destroyer_t m_destroyer;
};

// :TODO Allow custom allocators?
template <std::size_t N, typename R, typename... Args>
class SizableUniqueFunction;

template <std::size_t N, typename R, typename... Args>
class SizableUniqueFunction<N, R(Args...)> {
public:
    using call_t = R (*)(void* instance, Args&&... args);

    constexpr SizableUniqueFunction() = default;

    template <typename F>
    constexpr SizableUniqueFunction(F&& func) noexcept : m_type_erasured(std::forward<F>(func)) {
        m_caller = &details::caller<F, R, Args...>; // :TODO Why can't we take address of std::invoke?
    }
    SizableUniqueFunction(const SizableUniqueFunction&) = delete;
    SizableUniqueFunction& operator=(const SizableUniqueFunction&) = delete;

    SizableUniqueFunction(SizableUniqueFunction&&) = default;
    SizableUniqueFunction& operator=(SizableUniqueFunction&&) = default;

    R operator()(Args&&... args) { return m_caller(m_type_erasured.pointer(), std::forward<Args>(args)...); }

    bool valid() const { return m_type_erasured.valid(); }

    bool inlined() const { return m_type_erasured.inlined(); }

private:
    ErasuredType<sizeof(void*) * N> m_type_erasured;
    call_t m_caller;
};

template <typename R, typename... Args>
class UniqueFunction;

constexpr inline std::size_t UniqueFunctionDefaultSize = 4;

template <typename R, typename... Args>
class UniqueFunction<R(Args...)> : SizableUniqueFunction<UniqueFunctionDefaultSize, R(Args...)> {
public:
    using ttp::SizableUniqueFunction<UniqueFunctionDefaultSize, R(Args...)>::SizableUniqueFunction;
    using ttp::SizableUniqueFunction<UniqueFunctionDefaultSize, R(Args...)>::operator();
    using ttp::SizableUniqueFunction<UniqueFunctionDefaultSize, R(Args...)>::valid;
    using ttp::SizableUniqueFunction<UniqueFunctionDefaultSize, R(Args...)>::inlined;
};

} // namespace ttp
