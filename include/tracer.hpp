#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <coroutine>
#include <deque>
#include <mutex>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include "async.hpp"

#include "event_loop.hpp"
#include "task.hpp"

// How the heavy (blocking) disk write is handled.
enum class mode {
    sync_blocking,  // copy inline on the loop thread  -> capture stalls
    thread_only,    // hand off to a pool thread, no coroutine
    coroutine       // co_await a pool thread via a writer coroutine
};

// A captured frame: raw bytes plus a sequence number to spot drops.
struct packet {
    std::vector<uint8_t> bytes;
    uint64_t seq = 0;
};

// A minimal classic-pcap file writer (blocking by design). See tracer.cpp.
// Thread-safe: the simulated slow-disk sleep happens before taking the lock, so
// parallel writers can overlap their "disk time" but fwrite() stays serialised.
class pcap_file {
public:
    explicit pcap_file(const std::string& path);
    ~pcap_file();
    pcap_file(const pcap_file&) = delete;
    pcap_file& operator=(const pcap_file&) = delete;

    void write(const packet& p, unsigned disk_us = 0);  // sleep + locked fwrite
    void flush();

private:
    std::FILE* fp_ = nullptr;
    std::mutex file_mtx_;
};

// A coroutine-friendly FIFO channel. Because everything runs on the one event-
// loop thread, no mutex is needed: a reader that finds an empty channel
// suspends, and the producer resumes it directly when it pushes a packet.
template <typename T>
struct channel {
    std::deque<T> items_;
    std::coroutine_handle<> cont_ = nullptr;

    struct pop_awaiter {
        channel& c;
        bool await_ready() const noexcept { 
            return !c.items_.empty(); 
        }
        bool await_suspend(std::coroutine_handle<> h) noexcept { 
            c.cont_ = h; 
            return true; 
        }
        T await_resume() {
            T v = std::move(c.items_.front());
            c.items_.pop_front();
            return v;
        }
    };

    pop_awaiter pop() { 
        return {*this}; 
    }

    void push(T v) {
        items_.push_back(std::move(v));
        if (cont_) { 
            auto h = cont_; 
            cont_ = nullptr; 
            h.resume(); 
        }
    }

    bool empty() const noexcept { 
        return items_.empty(); 
    
    }
    bool try_pop(T& out) {
        if (items_.empty()) return false;
        out = std::move(items_.front());
        items_.pop_front();
        return true;
    }
};

// Shared counters (written from the loop thread and read by main).
struct counters {
    std::atomic<uint64_t> produced{0};
    std::atomic<uint64_t> written{0};
};

// Producer coroutine: makes a packet, then dispatches the blocking write
task<void> producer(io_context& ctx, thread_pool& pool, channel<packet>& ch,
                    pcap_file& pf, counters& st, mode how, unsigned disk_us,
                    std::mt19937& rng);

// Writer coroutine (used only in coroutine mode): pops a packet and co-awaits
// a pool job that does the blocking disk write on a worker thread. In thread
// mode there is no such coroutine — the producer hands each write straight to
// pool.run()
task<void> writer(io_context& ctx, thread_pool& pool, channel<packet>& ch,
                  pcap_file& pf, counters& st, unsigned disk_us);
