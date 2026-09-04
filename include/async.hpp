#pragma once

#include <condition_variable>
#include <coroutine>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "event_loop.hpp"


class thread_pool {
    using handle = std::coroutine_handle<>;
public:
    explicit thread_pool(unsigned n = 0);   // 0 -> hardware_concurrency
    ~thread_pool();
    thread_pool(const thread_pool&) = delete;
    thread_pool& operator=(const thread_pool&) = delete;

    void wait_idle(); 

    template <typename Fn>
    struct async_awaiter {
        io_context& ctx;
        thread_pool& pool;
        Fn fn;
        handle waiter_ = nullptr;

        bool await_ready() const noexcept;
        bool await_suspend(handle h) noexcept;
        void await_resume() noexcept;
    };

    template <typename Fn>
    async_awaiter<Fn> async(io_context& ctx, Fn&& fn);

    template <typename Fn>
    void run(Fn&& fn);

private:
    using job_t = std::function<void()>;
    void submit(job_t job, job_t done);
    void worker_loop(); 

    std::deque<std::pair<job_t, job_t>> jobs_;
    std::mutex m_;
    std::condition_variable cv_, idle_cv_;
    std::vector<std::thread> workers_;
    unsigned inflight_ = 0;
    bool stop_ = false;
};

#include "../src/async.tpp"
