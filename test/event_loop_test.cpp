#include "../include/event_loop.hpp"
#include "../src/task.cpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include <unistd.h>

int tests_run = 0;

void expect(bool condition, const char* message) {
    ++tests_run;
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

// Captures a task's handle and suspends it until the test submits that handle
// through io_context::post(). This is needed because task<void> is eager.
struct suspend_for_post {
    handle& suspended;

    bool await_ready() const noexcept {
        return false;
    }

    bool await_suspend(handle coroutine) const noexcept {
        suspended = coroutine;
        return true;
    }

    void await_resume() const noexcept {}
};

struct timer_wait {
    io_context& context;
    int milliseconds;

    bool await_ready() const noexcept {
        return false;
    }

    bool await_suspend(handle coroutine) const noexcept {
        context.add_timer(milliseconds, coroutine);
        return true;
    }

    void await_resume() const noexcept {}
};

struct readable_wait {
    io_context& context;
    int file_descriptor;

    bool await_ready() const noexcept {
        return false;
    }

    bool await_suspend(handle coroutine) const noexcept {
        context.add_readable(file_descriptor, coroutine);
        return true;
    }

    void await_resume() const noexcept {}
};

task<void> set_flag_after_post(handle& suspended, bool& flag) {
    co_await suspend_for_post{suspended};
    flag = true;
}

task<void> wait_for_timer(
    io_context& context,
    int milliseconds,
    bool& completed,
    std::chrono::steady_clock::duration& elapsed) {
    auto started = std::chrono::steady_clock::now();
    co_await timer_wait{context, milliseconds};
    elapsed = std::chrono::steady_clock::now() - started;
    completed = true;
}

task<void> wait_for_readable(
    io_context& context,
    int read_descriptor,
    bool& completed,
    char& received) {
    co_await readable_wait{context, read_descriptor};
    auto bytes_read = ::read(read_descriptor, &received, sizeof(received));
    completed = bytes_read == 1;
}

task<void> stop_after_post(
    io_context& context,
    handle& suspended,
    bool& executed) {
    co_await suspend_for_post{suspended};
    executed = true;
    context.stop();
}

void test_post() {
    io_context context;
    handle suspended{};
    bool executed = false;
    auto work = set_flag_after_post(suspended, executed);

    expect(static_cast<bool>(suspended),
           "the eager task should suspend before it is posted");
    context.post(suspended);
    context.run();

    std::cout << "[event-loop] post: executed=" << std::boolalpha << executed
              << '\n';
    expect(executed, "post() should schedule and resume the task");
}

void test_timer() {
    io_context context;
    bool completed = false;
    std::chrono::steady_clock::duration elapsed{};

    // The eager task registers its timer before context.run() is called.
    auto work = wait_for_timer(context, 20, completed, elapsed);
    context.run();

    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    std::cout << "[event-loop] timer: completed=" << std::boolalpha << completed
              << ", elapsed_ms=" << elapsed_ms << '\n';
    expect(completed, "add_timer() should resume its waiting task");
    expect(elapsed >= std::chrono::milliseconds{10},
           "the timer should not resume immediately");
}

void test_readable_descriptor() {
    int descriptors[2]{};
    expect(::pipe(descriptors) == 0, "pipe() should create test descriptors");

    io_context context;
    bool completed = false;
    char received = '\0';

    // This eager task registers the read descriptor and then suspends.
    auto work =
        wait_for_readable(context, descriptors[0], completed, received);

    std::thread writer([write_descriptor = descriptors[1]] {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        const char value = 'N';
        (void)::write(write_descriptor, &value, sizeof(value));
    });

    context.run();
    writer.join();

    std::cout << "[event-loop] readable: completed=" << std::boolalpha
              << completed << ", byte='" << received << "'\n";
    expect(completed, "add_readable() should resume its waiting task");
    expect(received == 'N', "the resumed task should read the written byte");

    ::close(descriptors[0]);
    ::close(descriptors[1]);
}

void test_cross_thread_post_and_stop() {
    io_context context;
    handle stopper_handle{};
    bool timer_completed = false;
    bool stop_executed = false;
    std::chrono::steady_clock::duration elapsed{};

    // The long timer keeps run() inside epoll_wait. Another thread posts the
    // suspended stopper task, which wakes the loop and calls stop().
    auto timer = wait_for_timer(context, 250, timer_completed, elapsed);
    auto stopper = stop_after_post(context, stopper_handle, stop_executed);

    expect(static_cast<bool>(stopper_handle),
           "the stopper task should expose its suspended handle");

    std::thread poster([&context, stopper_handle] {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        context.post(stopper_handle);
    });

    context.run();
    poster.join();

    std::cout << "[event-loop] cross-thread stop: posted=" << std::boolalpha
              << stop_executed << ", stopped=" << context.stopped() << '\n';
    expect(stop_executed, "post() should wake the loop from another thread");
    expect(context.stopped(), "stop() should mark the context as stopped");
}

int main() {
    test_post();
    test_timer();
    test_readable_descriptor();
    test_cross_thread_post_and_stop();

    std::cout << "[event-loop] completed " << tests_run << " checks.\n";
    return EXIT_SUCCESS;
}
