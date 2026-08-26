#pragma once 

#include <coroutine>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

using handle = std::coroutine_handle<>;

struct io_context {
    io_context();
    ~io_context();
    io_context(const io_context&) = delete;
    io_context& operator=(const io_context&) = delete;

    void add_readable(int fd, handle h);// register `fd` to be observed for readability; when readable, `h` is resumed once
    void add_timer(int ms, handle h);   // arm a one-shot timer; `h` is resumed when `ms` milliseconds elapse
    void post(handle h);                // wake a coroutine from any thread
    void stop();
    bool stopped() const;
    void run();                          // pump the loop until idle/stopped

private:
    int epfd_, wakefd_, timerfd_; // epoll fd, eventfd for wakeup, timerfd for sleep
    std::unordered_map<int, handle> handlers_; // fd -> coroutine handle
    std::vector<handle> pending_; // coroutines to resume
    std::mutex m_; // protects pending_
    bool stop_ = false;
};

struct read_ready_t {
    io_context& ctx;
    int fd; // file descriptor to wait for readability
    bool await_ready() const noexcept;
    bool await_suspend(handle h) noexcept;
    void await_resume() const noexcept;
};
inline read_ready_t read_ready(io_context& ctx, int fd);

struct sleep_for_t {
    io_context& ctx;
    int ms; // milliseconds to sleep
    bool await_ready() const noexcept;
    bool await_suspend(handle h) noexcept;
    void await_resume() const noexcept;
};
inline sleep_for_t sleep_for(io_context& ctx, int ms);