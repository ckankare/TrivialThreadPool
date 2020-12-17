#include <catch2/catch_test_macros.hpp>

#include "ttp/thread_pool.hpp"
#include <chrono>
#include <thread>

int func(int a, int b) { return 2 * a + b; }

TEST_CASE("Task returning reference") {
    ttp::ThreadPool pool(10);
    int v1 = 13;
    auto f1 = pool.async([&v1]() -> int& {
        v1 += 5;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        v1 += 10;
        return v1;
    });

    auto& r1 = f1.get();
    REQUIRE(&v1 == &r1);
    REQUIRE(v1 == 28);
}

TEST_CASE("Task returning movable-only type") {
    ttp::ThreadPool pool(10);
    int v1 = 13;
    auto f1 = pool.async([v1]() {
        auto result = std::make_unique<int>(42 * v1);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return result;
    });

    auto r1 = f1.get();
    REQUIRE(*r1 == 42 * 13);
}

TEST_CASE("Free function") {
    ttp::ThreadPool pool(10);
    std::vector<ttp::Future<int>> futures;
    for (int i = 0; i < 20; ++i) {
        auto f1 = pool.async(func, i, 3);
        futures.push_back(std::move(f1));
    }

    for (int i = 0; i < 20; ++i) {
        auto value = futures[i].get();
        REQUIRE(value == i * 2 + 3);
    }
}

TEST_CASE("Member function") {
    struct S {
        void func1() {
            func2(11);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        void func2(int b) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            a += b;
        }
        int a = 3;
    };

    S s1;
    S s2;
    ttp::ThreadPool pool(10);
    {
        auto f1 = pool.async(&S::func1, s1);
        auto f2 = pool.async(&S::func2, s2, 20);

        f1.get();
        f2.get();
    }

    REQUIRE(s1.a == 14);
    REQUIRE(s2.a == 23);

    {
        auto f1 = pool.async(&S::func1, s2);
        auto f2 = pool.async(&S::func2, s1, 30);

        f1.get();
        f2.get();
    }

    REQUIRE(s1.a == 44);
    REQUIRE(s2.a == 34);
}

TEST_CASE("Async tasks") {
    ttp::ThreadPool pool(10);
    std::vector<int> result(100);
    std::vector<ttp::Future<void>> futures;
    for (std::size_t i = 0; i < 20; ++i) {
        auto future = pool.async([&, i] {
            for (std::size_t j = 0; j < 5; ++j) {
                result[i * 5 + j] = static_cast<int>(j);
            }
        });
        futures.push_back(std::move(future));
    }
    for (auto& f : futures) {
        f.get();
    }

    REQUIRE(result.size() == 100);
    for (std::size_t i = 0; i < 100; ++i) {
        REQUIRE(result[i] == static_cast<int>(i % 5));
    }
}

TEST_CASE("Nested async tasks") {
    ttp::ThreadPool pool(10);
    std::vector<int> result(100);
    std::vector<ttp::Future<void>> futures;
    for (std::size_t i = 0; i < 20; ++i) {
        auto future = pool.async([&, i] {
            auto f1 = pool.async([&, i] {
                auto f2 = pool.async([&, i] {
                    auto f3 = pool.async([&, i] {
                        auto f4 = pool.async([&, i] {
                            auto f5 = pool.async([&, i] { result[i * 5 + 4] = static_cast<int>(4); });
                            f5.get();
                            result[i * 5 + 3] = static_cast<int>(3);
                        });
                        f4.get();
                        result[i * 5 + 2] = static_cast<int>(2);
                    });
                    f3.get();
                    result[i * 5 + 1] = static_cast<int>(1);
                });
                f2.get();
                result[i * 5 + 0] = static_cast<int>(0);
            });
            f1.get();
        });
        futures.push_back(std::move(future));
    }

    pool.wait(ttp::Wait::Async);

    REQUIRE(result.size() == 100);
    for (std::size_t i = 0; i < 100; ++i) {
        REQUIRE(result[i] == static_cast<int>(i % 5));
    }
}
