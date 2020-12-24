#include <catch2/catch_test_macros.hpp>

#include "ttp/unique_function.hpp"
#include <array>
#include <compare>
#include <functional>

namespace {

struct S {
    uint64_t a;
    uint64_t b;
    friend auto operator<=>(const S&, const S&) = default;
};

std::size_t constructor_counter = 0;
std::size_t move_counter = 0;
std::size_t copy_counter = 0;
std::size_t destructor_counter = 0;

void clear_counters() {
    constructor_counter = 0;
    move_counter = 0;
    copy_counter = 0;
    destructor_counter = 0;
}

template <std::size_t N>
struct S2 {
    S2(std::array<uint8_t, N>&& arr) : data(std::move(arr)) { constructor_counter += 1; }
    S2(const S2& rhs) {
        data = rhs.data;
        copy_counter += 1;
    }
    S2& operator=(const S2& rhs) {
        this->~S2();
        data = rhs.data;
        copy_counter += 1;
        return *this;
    }
    S2(S2&& rhs) {
        data = rhs.data;
        move_counter += 1;
    }
    S2& operator=(S2&& rhs) {
        this->~S2();
        data = rhs.data;
        move_counter += 1;
        return *this;
    }
    ~S2() { destructor_counter += 1; }
    friend auto operator<=>(const S2&, const S2&) = default;
    std::array<uint8_t, N> data;
};

} // namespace

TEST_CASE("Inlined data", "[Storage]") {
    ttp::Storage<4> s(static_cast<uint32_t>(123));
    REQUIRE(s.is_inlined());
    REQUIRE(s.cast<int32_t>() == 123);

    auto s2 = std::move(s);
    REQUIRE(s2.is_inlined());
    REQUIRE(s2.cast<int32_t>() == 123);

    s = ttp::Storage<4>(static_cast<uint32_t>(128'000));
    REQUIRE(s.is_inlined());
    REQUIRE(s.cast<uint32_t>() == 128'000);

    // And then we test that we can store heap-allocated data
    // when we previously had inlined data.
    s = ttp::Storage<4>(static_cast<int64_t>(500));
    REQUIRE(!s.is_inlined());
    REQUIRE(s.cast<int64_t>() == 500);
}

TEST_CASE("Heap allocated data", "[Storage]") {
    ttp::Storage<4> s(static_cast<int64_t>(123));
    REQUIRE(!s.is_inlined());
    REQUIRE(s.cast<int64_t>() == 123);

    auto s2 = std::move(s);
    REQUIRE(!s2.is_inlined());
    REQUIRE(s2.cast<int64_t>() == 123);

    s = ttp::Storage<4>(static_cast<int64_t>(128'000));
    REQUIRE(!s.is_inlined());
    REQUIRE(s.cast<uint32_t>() == 128'000);

    // And then we test that we can store inlined data
    // when we previously had heap-allocated data.
    s = ttp::Storage<4>(static_cast<int32_t>(500));
    REQUIRE(s.is_inlined());
    REQUIRE(s.cast<int32_t>() == 500);
}

TEST_CASE("Inlined allocated constructors and destructors", "[ErasuredType]") {
    constexpr std::size_t M = 8;
    constexpr std::size_t N = 8;

    {
        ttp::ErasuredType<M> instance(S2(std::array<uint8_t, N>{1, 2, 3, 4, 5, 6, 7, 8}));
        auto& value = instance.cast<S2<N>>();

        REQUIRE(value.data == std::array<uint8_t, N>{1, 2, 3, 4, 5, 6, 7, 8});
    }
    REQUIRE(constructor_counter == 1);
    REQUIRE(move_counter == 1);
    REQUIRE(copy_counter == 0);
    REQUIRE(destructor_counter == 2);
    clear_counters();

    {
        ttp::ErasuredType<M> instance1(S2(std::array<uint8_t, N>{9, 10, 11, 12, 13, 14, 15, 16}));
        REQUIRE(destructor_counter == 1);

        auto instance2 = std::move(instance1);
        REQUIRE(destructor_counter == 1);

        auto instance3 = std::move(instance2);
        REQUIRE(destructor_counter == 1);

        auto& value = instance3.cast<S2<N>>();

        REQUIRE(value.data == std::array<uint8_t, N>{9, 10, 11, 12, 13, 14, 15, 16});
    }
    REQUIRE(constructor_counter == 1);
    REQUIRE(move_counter == 3); // Inlined storage must use the move constructor/assignment
    REQUIRE(copy_counter == 0);
    REQUIRE(destructor_counter == 2);
    clear_counters();
}

TEST_CASE("Heap allocated constructors and destructors", "[ErasuredType]") {
    constexpr std::size_t M = 4;
    constexpr std::size_t N = 8;

    {
        ttp::ErasuredType<M> instance(S2(std::array<uint8_t, N>{1, 2, 3, 4, 5, 6, 7, 8}));
        auto& value = instance.cast<S2<N>>();

        REQUIRE(value.data == std::array<uint8_t, N>{1, 2, 3, 4, 5, 6, 7, 8});
    }
    REQUIRE(constructor_counter == 1);
    REQUIRE(move_counter == 1);
    REQUIRE(copy_counter == 0);
    REQUIRE(destructor_counter == 2);
    clear_counters();

    {
        ttp::ErasuredType<M> instance1(S2(std::array<uint8_t, N>{9, 10, 11, 12, 13, 14, 15, 16}));
        REQUIRE(destructor_counter == 1);

        auto instance2 = std::move(instance1);
        REQUIRE(destructor_counter == 1);

        auto instance3 = std::move(instance2);
        REQUIRE(destructor_counter == 1);

        auto& value = instance3.cast<S2<N>>();

        REQUIRE(value.data == std::array<uint8_t, N>{9, 10, 11, 12, 13, 14, 15, 16});
    }
    REQUIRE(constructor_counter == 1);
    REQUIRE(move_counter == 1); // Heap allocated storage just moves the pointer
    REQUIRE(copy_counter == 0);
    REQUIRE(destructor_counter == 2);
    clear_counters();
}

TEST_CASE("Trivial lambda", "[UniqueFunction]") {
    ttp::UniqueFunction<int(int, int)> func = [](int a, int b) { return a * b; };

    auto value = func(10, 13);
    REQUIRE(value == 130);
}

TEST_CASE("Non-returning lambda", "[UniqueFunction]") {
    int value = 10;
    ttp::UniqueFunction<void(int, int, int&)> func = [](int a, int b, int& value) { value += a * b; };

    func(10, 13, value);
    REQUIRE(value == 140);
}

TEST_CASE("Lambda returning reference", "[UniqueFunction]") {
    int value = 10;
    ttp::UniqueFunction<int&(int&, int)> func = [](int& value, int a) -> int& {
        value += a;
        return value;
    };

    int& result = func(value, 15);
    REQUIRE(&result == &value);
    REQUIRE(value == 25);
}

TEST_CASE("Move-only types", "[UniqueFunction]") {
    auto a = std::make_unique<int>(10);
    ttp::UniqueFunction<std::unique_ptr<int>(std::unique_ptr<int>, int)> func =
        [a = std::move(a)](std::unique_ptr<int> b, int c) {
            auto result = *(a) * (*b) + c;
            return std::make_unique<int>(result);
        };

    auto value = func(std::make_unique<int>(11), 12);
    REQUIRE(*value == 122);
}

TEST_CASE("Capturing lambda", "[UniqueFunction]") {
    int value = 10;
    ttp::UniqueFunction<void(int, int)> func1 = [&value](int a, int b) { value += a + b; };

    func1(10, 13);
    REQUIRE(value == 33);

    ttp::UniqueFunction<int()> func2 = [value, a = 1, b = 2]() mutable {
        value += a + b;
        return value;
    };

    REQUIRE(func2() == 36);
    REQUIRE(value == 33);
}

TEST_CASE("Foo", "[UniqueFunction]") {
    auto func = [](std::unique_ptr<int> a, int b) -> int { return (*a) * b; };

    ttp::UniqueFunction<int()> task = [func = std::move(func), arg1 = std::make_unique<int>(22),
                                       arg2 = 33]() mutable -> int {
        return std::invoke(std::move(func), std::move(arg1), std::move(arg2));
    };

    REQUIRE(task() == 33 * 22);
}
// :TODO Tests for member functions and free functions.
