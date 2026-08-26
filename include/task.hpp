#pragma once

#include <coroutine>
#include <exception>    
#include <optional>
#include <utility>

using handle = std::coroutine_handle<>;

template <typename Promise>
struct final_awaiter {
    bool await_ready() const noexcept;
    handle await_suspend(std::coroutine_handle<Promise> self) const noexcept;
    void await_resume() const noexcept;
};

template <typename T>
struct task {

private:
    handle_type h_;

public:
    struct promise_type {
        task get_return_object();
        std::suspend_never initial_suspend() noexcept;
        final_awaiter<promise_type> final_suspend() noexcept;
        void unhandled_exception();
        void return_value(T v);

        std::optional<T> val_;
        std::exception_ptr ex_;
        handle cont_ = nullptr;
    };
    using handle_type = std::coroutine_handle<promise_type>;

    task() = default;
    ~task();
    task(task&& o) noexcept; // move constructor
    task& operator=(task&& o) noexcept;
    task(const task&) = delete;
    task& operator=(const task&) = delete;

    bool await_ready() const noexcept;
    void await_suspend(handle waiter) noexcept;
    T await_resume();
};
