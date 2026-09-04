#pragma once

#include <iostream>

inline thread_pool::thread_pool(unsigned n) {
    if (n == 0) {
        n = std::thread::hardware_concurrency();
    }
    if (n == 0) {
        n = 1;
    }
    for (unsigned i = 0; i < n; ++i) {
        workers_.emplace_back([this] {
             worker_loop();
        });
    }
}

inline thread_pool::~thread_pool() {
    {
        std::lock_guard<std::mutex> lk(m_);
        stop_ = true;
    }
    cv_.notify_all(); // wake all workers so they can exit
    for (auto& t : workers_) {
        t.join();
    }
}

inline void thread_pool::wait_idle() {
    std::unique_lock<std::mutex> lk(m_);
    idle_cv_.wait(lk, [this] { 
        return inflight_ == 0 && jobs_.empty(); 
    });
}

inline void thread_pool::worker_loop() {
    for (;;) {
        job_t job, done;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [this] { 
                return stop_ || !jobs_.empty(); // wait until there is a job or we are stopping
            });

            if (stop_ && jobs_.empty()) { // break the loop if we are stopping and there are no more jobs
                return;
            }
            std::tie(job, done) = std::move(jobs_.front());
            jobs_.pop_front();
        }
        job(); // run the job
        done(); // run the done callback
        {
            std::lock_guard<std::mutex> lk(m_);
            --inflight_;
            if (inflight_ == 0 && jobs_.empty()) {
                idle_cv_.notify_all();
            }
        }
    }
}

inline void thread_pool::submit(job_t job, job_t done) {
    {
        std::lock_guard<std::mutex> lk(m_);
        if (stop_) {
            std::cerr << "thread_pool::submit called after stop_ is set\n";
            return;
        }
        inflight_++;
        jobs_.emplace_back(std::move(job), std::move(done));
    }
    // Notify a worker thread
    cv_.notify_one();
}

template <typename Fn>
thread_pool::async_awaiter<Fn> thread_pool::async(io_context& ctx, Fn&& fn) {
    // The `async` function creates an `async_awaiter` object that holds references to the `io_context`, 
    // the `thread_pool`, and the function `fn`. It also initializes the `waiter_` member to `nullptr`, 
    // indicating that there is no coroutine waiting for the result yet. The function perfectly forwards the `fn` argument, 
    // preserving its value category (lvalue or rvalue). This allows the caller to pass in any callable object (like a lambda, function pointer, or functor) 
    // without unnecessary copies or moves. The returned `async_awaiter` can then be used in a coroutine to await the completion of the asynchronous operation represented by `fn`.
    return {ctx, *this, std::forward<Fn>(fn)};
}

template <typename Fn>
void thread_pool::run(Fn&& fn) {
    // The `done` callback is a lambda that does nothing. This is because we don't need to do anything
    // after the job is completed in this case. The `submit` function requires a `done` callback, so we provide an empty one.
    // This allows the job to be executed in the thread pool without any additional actions after its completion.
    // perfectly forward the `fn` argument, preserving its value category (lvalue or rvalue).
    submit(std::forward<Fn>(fn), []{});
}

template <typename Fn>
bool thread_pool::async_awaiter<Fn>::await_ready() const noexcept {
    // The `await_ready` method checks if the coroutine is ready to proceed without suspension.
    // In this implementation, it always returns `false`, indicating that the coroutine should always be
    // suspended and resumed later. This is because the asynchronous operation represented by `fn` is expected to take some time to complete,
    // and we want to ensure that the coroutine is suspended until the operation is finished.
    return false;
}

template <typename Fn>
bool thread_pool::async_awaiter<Fn>::await_suspend(handle h) noexcept {
    // The `await_suspend` method is called when the coroutine is suspended. It takes a
    // coroutine handle `h` as an argument, which represents the suspended coroutine. In this implementation,
    // we store the handle in the `waiter_` member variable, allowing us to resume the coroutine later when the asynchronous operation is complete. The method returns `true`, indicating that the
    // coroutine should be suspended and resumed later. This is important for the correct functioning of the asynchronous operation, as it allows the coroutine to yield control and wait for the operation to finish before continuing execution.
    waiter_ = h;
    pool.submit(fn, [this] { 
        ctx.post(waiter_); 
    });  // resume on the loop

    return true;
}

template <typename Fn>
void thread_pool::async_awaiter<Fn>::await_resume() noexcept {
    // The `await_resume` method is called when the coroutine is resumed after being suspended.
    // In this implementation, it does nothing, as there is no specific action needed when the
    // coroutine resumes. The method is marked as `noexcept`, indicating that it does not throw exceptions.
    // This is important for ensuring that the coroutine can be resumed safely without unexpected errors.
}
