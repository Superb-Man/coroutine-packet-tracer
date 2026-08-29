#include "../src/task.cpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
int tests_run = 0;
handle paused_coroutine{};

void expect(bool condition, const char* message) {
    ++tests_run;
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

task<int> return_number() {
    co_return 42;
}

task<std::string> return_text() {
    co_return std::string{"netio"};
}

task<void> return_void() {
    co_return;
}

task<int> await_value_task() {
    int value = co_await return_number();
    co_return value + 1;
}

task<int> await_void_task() {
    co_await return_void();
    co_return 7;
}

task<int> throw_error() {
    throw std::runtime_error{"task failed"};
    co_return 0;
}

task<void> throw_void_error() {
    throw std::runtime_error{"void task failed"};
    co_return;
}

// Suspends once and exposes the coroutine handle to the test. Resuming this
// handle lets us verify that final_awaiter transfers control to the parent task.
struct pause_once {
    bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(handle coroutine) const noexcept {
        paused_coroutine = coroutine;
    }

    void await_resume() const noexcept {}
};

task<int> delayed_value() {
    co_await pause_once{};
    co_return 10;
}

task<int> await_delayed_value(bool& parent_finished) {
    int value = co_await delayed_value();
    parent_finished = true;
    co_return value + 5;
}

void test_immediate_results() {
    auto number = return_number();
    bool number_ready = number.await_ready();
    int number_result = number.await_resume();
    expect(number_ready, "an eager value task should already be complete");
    expect(number_result == 42, "task<int> should return its value");

    auto text = return_text();
    std::string text_result = text.await_resume();
    expect(text_result == "netio", "task<string> should return its value");

    auto nothing = return_void();
    bool void_ready = nothing.await_ready();
    expect(void_ready, "an eager void task should already be complete");
    nothing.await_resume();

    std::cout << "[task] immediate: ready=" << std::boolalpha << number_ready
              << ", number=" << number_result << ", text=\"" << text_result
              << "\", void_ready=" << void_ready << '\n';
}

void test_nested_tasks() {
    auto value = await_value_task();
    int nested_result = value.await_resume();
    expect(nested_result == 43, "co_await should obtain a task value");

    auto after_void = await_void_task();
    int after_void_result = after_void.await_resume();
    expect(after_void_result == 7, "task<void> should be awaitable");

    std::cout << "[task] nested: value=" << nested_result
              << ", after_void=" << after_void_result << '\n';
}

void test_symmetric_transfer() {
    paused_coroutine = {};
    bool parent_finished = false;
    auto parent = await_delayed_value(parent_finished);

    expect(static_cast<bool>(paused_coroutine),
           "the child task should expose its suspended coroutine");
    expect(!parent_finished, "the parent must wait for the child task");
    bool ready_before_resume = parent.await_ready();
    expect(!ready_before_resume, "the waiting parent should be suspended");

    paused_coroutine.resume();

    expect(parent_finished, "child completion should resume the parent");
    bool ready_after_resume = parent.await_ready();
    int transferred_result = parent.await_resume();
    expect(ready_after_resume, "the resumed parent should complete");
    expect(transferred_result == 15,
           "the parent should receive the delayed child value");

    std::cout << "[task] continuation: ready_before=" << ready_before_resume
              << ", ready_after=" << ready_after_resume
              << ", result=" << transferred_result << '\n';
}

void test_exception_propagation() {
    auto failing = throw_error();
    bool caught = false;
    std::string value_error;
    try {
        (void)failing.await_resume();
    } catch (const std::runtime_error& error) {
        value_error = error.what();
        caught = value_error == "task failed";
    }
    expect(caught, "task<T> should rethrow its stored exception");

    auto failing_void = throw_void_error();
    caught = false;
    std::string void_error;
    try {
        failing_void.await_resume();
    } catch (const std::runtime_error& error) {
        void_error = error.what();
        caught = void_error == "void task failed";
    }
    expect(caught, "task<void> should rethrow its stored exception");

    std::cout << "[task] exceptions: value=\"" << value_error
              << "\", void=\"" << void_error << "\"\n";
}

void test_move_ownership() {
    auto source = return_number();
    auto moved = std::move(source);

    bool source_empty = source.await_ready();
    int moved_result = moved.await_resume();
    expect(source_empty, "a moved-from task should be empty");
    expect(moved_result == 42,
           "move construction should preserve the coroutine result");

    auto assigned = return_number();
    auto replacement = await_value_task();
    assigned = std::move(replacement);

    bool replacement_empty = replacement.await_ready();
    int assigned_result = assigned.await_resume();
    expect(replacement_empty, "a move-assigned source should be empty");
    expect(assigned_result == 43,
           "move assignment should transfer coroutine ownership");

    std::cout << "[task] moves: source_empty=" << source_empty
              << ", moved_value=" << moved_result
              << ", assigned_value=" << assigned_result << '\n';
}

int main() {
    test_immediate_results();
    test_nested_tasks();
    test_symmetric_transfer();
    test_exception_propagation();
    test_move_ownership();

    std::cout << "[task] completed " << tests_run << " checks.\n";
    return EXIT_SUCCESS;
}
