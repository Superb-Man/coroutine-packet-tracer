#include "../include/task.hpp"


template <typename Promise>
bool final_awaiter<Promise>::await_ready() const noexcept {
    return false;
}   

// await_suspend for a final_suspend: hand control to the awaiting coroutine, if any.
// If nobody is waiting, return a noop_coroutine so the final_suspend() suspends
// and the coroutine is destroyed immediately after final_suspend() returns.
template <typename Promise>
handle final_awaiter<Promise>::await_suspend(handle self) const noexcept {
    if (self.promise().cont_)
        return std::exchange(self.promise().cont_, nullptr);
    return std::noop_coroutine();   
}

// await_resume for a final_suspend: nothing to return, and no exceptions can be thrown.
template <typename Promise>
void final_awaiter<Promise>::await_resume() const noexcept {}