#pragma once

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "unique_function.hpp"

namespace ttp {

enum class Wait { Async, Sync };

namespace details {

    class Task {
    public:
        template <typename F, typename... Args>
        explicit Task(std::future<std::invoke_result_t<F, Args...>>& future, F&& func, Args&&... args) noexcept
            : m_ready(false) {
            using result_t = std::invoke_result_t<F, Args...>;
            m_worked_on.clear();

            ttp::UniqueFunction<result_t()> task = [func = std::forward<F>(func),
                                                    ... args = std::forward<Args>(args)]() mutable -> result_t {
                // :NOTE For lower than C++20, args can be packed in a tuple instead
                // 'args = std::make_tuple(std::forward<Args>(args)...)'
                // and then used with 'std::apply'.
                return std::invoke(func, std::forward<Args>(args)...);
            };

            std::promise<result_t> promise;
            future = promise.get_future();

            std::promise<void> task_barrier;
            m_future = task_barrier.get_future();

            m_task = [task = std::move(task), promise = std::move(promise),
                      task_barrier = std::move(task_barrier)]() mutable noexcept {
                try {
                    if constexpr (std::is_same_v<result_t, void>) {
                        task();
                        promise.set_value();
                    } else {
                        promise.set_value(task());
                    }
                } catch (...) {
                    promise.set_exception(std::current_exception());
                }
                task_barrier.set_value();
            };

            assert(m_task.valid());
        }

        inline void try_run() noexcept {
            if (!m_worked_on.test_and_set()) {
                assert(m_task.valid());
                assert(!m_ready);
                m_task();
                m_ready = true;
            }
        }

        inline void wait() const noexcept { m_future.wait(); }

        inline bool ready() const { return m_ready; }

    private:
        std::atomic_flag m_worked_on;
        std::atomic<bool> m_ready;
        std::future<void> m_future;
        ttp::SizableUniqueFunction<18, void()> m_task;
    };
} // namespace details

template <typename T>
class Future {
public:
    Future(Future&&) = default;
    Future& operator=(Future&&) = default;

    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;

    ~Future() noexcept = default;

    inline void wait() const noexcept { m_future.wait(); }

    void get() requires std::is_same_v<T, void> {
        m_task->try_run();
        assert(m_future.valid());
        m_future.get();
    }

    [[nodiscard]] T get() {
        m_task->try_run();
        assert(m_future.valid());
        return m_future.get();
    }

private:
    friend class ThreadPool;
    Future(std::shared_ptr<details::Task> task, std::future<T>&& future) noexcept
        : m_task(std::move(task)), m_future(std::move(future)) {}

    std::shared_ptr<details::Task> m_task;
    std::future<T> m_future;
};

class ThreadPool {
public:
    ThreadPool(std::size_t pool_size) noexcept : m_pool_size(pool_size) {
        m_workers.reserve(m_pool_size);
        m_ongoing = 0u;
        m_quit = false;
        for (std::size_t i = 0; i < m_pool_size; ++i) {
            m_workers.emplace_back([this]() { do_work(); });
        }
    }

    ~ThreadPool() noexcept {
        m_quit = true;
        m_work_condition.notify_all();
        for (auto& worker : m_workers) {
            worker.join();
        }
    }

    template <typename F, typename T, typename... Args>
    auto async(F&& func, T& object, Args&&... args) -> Future<typename std::invoke_result_t<F, T*, Args...>> {
        return async(std::forward<F>(func), &object, std::forward<Args>(args)...);
    }

    template <typename F, typename... Args>
    requires(std::is_invocable_v<F, Args...>) auto async(F&& func, Args&&... args)
        -> Future<std::invoke_result_t<F, Args...>> {
        using result_t = std::invoke_result_t<F, Args...>;
        std::future<result_t> future;
        auto wrapped_task = std::make_shared<details::Task>(future, std::forward<F>(func), std::forward<Args>(args)...);
        {
            std::lock_guard<std::mutex> lock(m_task_mutex);

            m_tasks.push(wrapped_task);
        }
        m_work_condition.notify_one();
        auto wrapped_future = Future<result_t>(wrapped_task, std::move(future));
        return wrapped_future;
    }

    template <typename F, typename... Args>
    auto async([[maybe_unused]] F&& func, [[maybe_unused]] Args&&... args) {
        static_assert(std::is_invocable_v<F, Args...>, "Couldn't deduce function");
    }

    inline void wait(Wait how) noexcept {
        if (how == Wait::Async) {
            while (true) {
                std::unique_lock<std::mutex> lock(m_task_mutex);
                if (m_tasks.empty()) {
                    break;
                }
                ++m_ongoing;
                auto task = std::move(m_tasks.front());
                m_tasks.pop();
                lock.unlock();

                task->try_run();

                --m_ongoing;
            }
        }

        std::unique_lock<std::mutex> lock(m_task_mutex);
        m_complete_condition.wait(lock, [this]() { return m_ongoing == 0; });
    }

    inline bool is_working() const noexcept { return m_ongoing > 0 || tasks() > 0; }
    inline std::size_t tasks() const noexcept {
        std::lock_guard<std::mutex> lock(m_task_mutex);
        return m_tasks.size();
    }
    inline std::size_t pool_size() const { return m_pool_size; }
    inline std::size_t hardware_cores() const { return std::thread::hardware_concurrency(); }

private:
    inline void do_work() noexcept {
        std::unique_lock<std::mutex> lock(m_task_mutex);
        while (true) {
            m_work_condition.wait(lock, [this]() { return m_quit || !m_tasks.empty(); });
            if (m_quit) {
                break;
            }

            ++m_ongoing;
            auto task = std::move(m_tasks.front());
            m_tasks.pop();
            lock.unlock();

            task->try_run();

            --m_ongoing;
            m_complete_condition.notify_one();
            lock.lock();
        }
    }

    std::size_t m_pool_size;
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_quit;
    std::atomic<std::size_t> m_ongoing;
    std::condition_variable m_work_condition;
    std::condition_variable m_complete_condition;
    std::queue<std::shared_ptr<details::Task>> m_tasks;
    mutable std::mutex m_task_mutex;
};

} // namespace ttp
