#include "../include/task.hpp"

// final_awaiter

// Always suspend at the final suspend point. This keeps the completed coroutine
// frame alive until the task object that owns it performs the destruction.
template <typename Promise>
bool final_awaiter<Promise>::await_ready() const noexcept {
    return false;
}

// Hand control directly to the coroutine that awaited this task. This symmetric
// transfer avoids growing the native call stack. If nobody is waiting, transfer
// to noop_coroutine; the task owner will still destroy the suspended frame later.
template <typename Promise>
handle final_awaiter<Promise>::await_suspend(
    std::coroutine_handle<Promise> self) const noexcept {
    if (self.promise().cont_) {
        return std::exchange(self.promise().cont_, handle{});
    }
    return std::noop_coroutine();
}

// A coroutine cannot continue normally after final suspension. This no-op exists
// only to complete the awaiter interface.
template <typename Promise>
void final_awaiter<Promise>::await_resume() const noexcept {}


// task<T>::promise_type

// Bind the returned task to the coroutine frame represented by this promise.
template <typename T>
task<T> task<T>::promise_type::get_return_object() {
    return task{handle_type::from_promise(*this)};
}

// task<T> is eager, so its body begins executing immediately after creation.
template <typename T>
std::suspend_never task<T>::promise_type::initial_suspend() noexcept {
    return {};
}

// Suspend the completed frame and let final_awaiter resume its continuation.
template <typename T>
final_awaiter<typename task<T>::promise_type>
task<T>::promise_type::final_suspend() noexcept {
    return {};
}

// Preserve an unhandled exception so it can be rethrown by await_resume().
template <typename T>
void task<T>::promise_type::unhandled_exception() {
    ex_ = std::current_exception();
}

// Store the returned value inside the coroutine frame until it is consumed.
template <typename T>
void task<T>::promise_type::return_value(T value) {
    val_.emplace(std::move(value));
}


// task<T> lifetime and awaiter interface

template <typename T>
task<T>::task(handle_type h) noexcept {
    h_ = h;
}

template <typename T>
task<T>::~task() {
    if (h_) {
        h_.destroy();
    }
}

template <typename T>
task<T>::task(task&& other) noexcept {
    h_ = std::exchange(other.h_, handle_type{});
}

template <typename T>
task<T>& task<T>::operator=(task&& other) noexcept {
    if (this != &other) {
        if (h_) {
            h_.destroy();
        }
        h_ = std::exchange(other.h_, handle_type{});
    }
    return *this;
}

template <typename T>
bool task<T>::await_ready() const noexcept {
    return !h_ || h_.done();
}

template <typename T>
void task<T>::await_suspend(handle waiter) noexcept {
    h_.promise().cont_ = waiter;
}

template <typename T>
T task<T>::await_resume() {
    if (h_.promise().ex_) std::rethrow_exception(h_.promise().ex_);
    return std::move(*h_.promise().val_);
}


// task<void>

inline task<void> task<void>::promise_type::get_return_object() {
    return task{handle_type::from_promise(*this)};
}

inline std::suspend_never task<void>::promise_type::initial_suspend() noexcept {
    return {};
}

inline final_awaiter<task<void>::promise_type>
task<void>::promise_type::final_suspend() noexcept {
    return {};
}

// Preserve an exception for propagation through await_resume().
inline void task<void>::promise_type::unhandled_exception() {
    ex_ = std::current_exception();
}

inline void task<void>::promise_type::return_void() noexcept {}

inline task<void>::task(handle_type h) noexcept { 
    h_ = h; 
}

inline task<void>::~task() {
    if (h_) {
        h_.destroy();
    }
}

inline task<void>::task(task&& other) noexcept {
    h_ = std::exchange(other.h_, handle_type{});
}

inline task<void>& task<void>::operator=(task&& other) noexcept {
    if (this != &other) {
        if (h_) {
            h_.destroy();
        }
        h_ = std::exchange(other.h_, handle_type{});
    }
    return *this;
}

inline bool task<void>::await_ready() const noexcept {
    return !h_ || h_.done();
}

inline void task<void>::await_suspend(handle waiter) noexcept {
    h_.promise().cont_ = waiter;
}

inline void task<void>::await_resume() {
    if (h_.promise().ex_) {
        std::rethrow_exception(h_.promise().ex_);
    }
}
