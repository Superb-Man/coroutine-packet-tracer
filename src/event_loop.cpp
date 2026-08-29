#include "../include/event_loop.hpp"

#include <cerrno>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>


io_context::io_context()
    : epfd_(epoll_create1(EPOLL_CLOEXEC)),
      wakefd_(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)),
      timerfd_(-1) {
    // register wakefd_ to be observed for readability; when readable, the loop is woken up
    // this is used to wake the loop from another thread when a coroutine is posted
    if (epfd_ >= 0 && wakefd_ >= 0) {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = wakefd_;
        epoll_ctl(epfd_, EPOLL_CTL_ADD, wakefd_, &ev);
    }
}

io_context::~io_context() {
    if (timerfd_ >= 0) ::close(timerfd_);
    if (wakefd_ >= 0)  ::close(wakefd_);
    if (epfd_ >= 0)    ::close(epfd_);
}

void io_context::stop() {
    stop_ = true;
    post(std::noop_coroutine()); // wake the loop so it notices stop_ and exits run()
}

void io_context::add_readable(int fd, handle h) {
    handlers_[fd] = h;                    // overwrite any stale entry  
    add_fd_helper(fd, EPOLLIN);
}

void io_context::add_timer(int ms, handle h) {
    // arm a one-shot timer; `h` is resumed when `ms` milliseconds elapse
    if (timerfd_ < 0) { // create timerfd_ if not already created
        timerfd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
        add_fd_helper(timerfd_, EPOLLIN);
    }
    handlers_[timerfd_] = h; 
    itimerspec its{};
    its.it_value.tv_sec  = ms / 1000;
    its.it_value.tv_nsec = (ms % 1000) * 1'000'000L;
    timerfd_settime(timerfd_, 0, &its, nullptr);
}

void io_context::post(handle h) {
    // wake coroutine h from any thread
    // add `h` to pending_ and wake the loop by writing to wakefd_
    {
        std::lock_guard<std::mutex> lk(m_);
        pending_.push_back(h);
    }

    uint64_t one = 1;
    auto r = ::write(wakefd_, &one, sizeof one); (void)r;
}

void io_context::run() {
    epoll_event evs[64];

    while(!stop_) {
        std::vector<handle> v;
        {
            std::lock_guard<std::mutex> lk(m_);
            // if (pending_.empty()) {
            //     return;
            // }
            v.swap(pending_);
        }

        for (handle h : v) h.resume();

        if (handlers_.empty() && pending_.empty()) break;  // idle, exit run()
        
        // wait for events on epfd_
        // n is the number of triggered events returned in evs buffer
        int n = epoll_wait(epfd_, evs, 64, -1);
        if (n < 0) {
            if (errno == EINTR) continue; // interrupted by signal, retry
            break; // error, exit run()
        }

        for (int i = 0; i < n; ++i) {
            int fd = evs[i].data.fd;
            if (fd == wakefd_) { // wakefd_ is readable, drain it
                // wakefd_ is an eventfd, and when it is readable, 
                // it means that some thread has written to it to wake up the loop.
                // we need to read from it to clear the event 
                // and allow it to be readable again in the future.
                uint64_t x;
                auto r = ::read(wakefd_, &x, sizeof x); (void)r;
                continue;
            }
            auto it = handlers_.find(fd);
            if (it == handlers_.end()) continue; // no handler for this fd
            handle h = it->second;
            handlers_.erase(it); // remove handler after resuming
            if (fd == timerfd_) { // timerfd_ is readable, drain it
                uint64_t x;
                auto r = ::read(timerfd_, &x, sizeof x); (void)r;
            }
            h.resume(); // resume the coroutine associated with this fd
        }
    }
}

bool io_context::stopped() const {
    return stop_;
}

void io_context::add_fd_helper(int fd, uint32_t events) {
    // register `fd` to be observed for `events`;
    epoll_event ev{};                     
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev);
}