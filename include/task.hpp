#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

using handle = std::coroutine_handle<>;

// Transfers execution from a completed task to the coroutine awaiting it.
template <typename Promise>
struct final_awaiter {
    bool await_ready() const noexcept;
    handle await_suspend(std::coroutine_handle<Promise> self) const noexcept;
    void await_resume() const noexcept;
};

template <typename T>
class task {
public:
    struct promise_type {
        task get_return_object();
        std::suspend_never initial_suspend() noexcept;
        final_awaiter<promise_type> final_suspend() noexcept;
        void unhandled_exception();
        void return_value(T value);

        std::optional<T> val_;
        std::exception_ptr ex_;
        handle cont_{};
    };

    using handle_type = std::coroutine_handle<promise_type>;

    task() = default;
    ~task();

    task(task&& other) noexcept;
    task& operator=(task&& other) noexcept;

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    bool await_ready() const noexcept;
    void await_suspend(handle waiter) noexcept;
    T await_resume();

private:
    explicit task(handle_type h) noexcept;

    handle_type h_{};
};

template <>
class task<void> {
public:
    struct promise_type {
        task get_return_object();
        std::suspend_never initial_suspend() noexcept;
        final_awaiter<promise_type> final_suspend() noexcept;
        void unhandled_exception();
        void return_void() noexcept;

        std::exception_ptr ex_;
        handle cont_{};
    };

    using handle_type = std::coroutine_handle<promise_type>;

    task() = default;
    ~task();

    task(task&& other) noexcept;
    task& operator=(task&& other) noexcept;

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    bool await_ready() const noexcept;
    void await_suspend(handle waiter) noexcept;
    void await_resume();

private:
    explicit task(handle_type h) noexcept;

    handle_type h_{};
};