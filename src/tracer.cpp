#include "../include/tracer.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <thread>


pcap_file::pcap_file(const std::string& path) {
    fp_ = std::fopen(path.c_str(), "wb");
    if (!fp_) throw std::runtime_error("cannot open " + path);
    struct {
        uint32_t magic = 0xa1b2c3d4u;
        uint16_t vmajor = 2, vminor = 4;
        int32_t zone = 0;
        uint32_t sig = 0, snaplen = 65535, network = 1;  // 1 = Ethernet
    } h;
    std::fwrite(&h, sizeof h, 1, fp_);
}

pcap_file::~pcap_file() {
    if (fp_) std::fclose(fp_);
}

void pcap_file::write(const packet& p, unsigned disk_us) {
    // Optional simulated slow disk: a *blocking* sleep. It happens before the
    // file lock so several worker threads may be "busy with the disk" at once;
    // only the actual fwrite() is serialised on file_mtx_.
    if (disk_us) std::this_thread::sleep_for(std::chrono::microseconds(disk_us));

    struct { 
        uint32_t ts; 
        uint32_t us;
        uint32_t incl; 
        uint32_t orig; 
    } r;
    r.ts = 1;
    r.us = p.seq;
    r.incl = (uint32_t)p.bytes.size();
    r.orig = r.incl;

    std::lock_guard<std::mutex> lk(file_mtx_);
    std::fwrite(&r, sizeof r, 1, fp_);
    std::fwrite(p.bytes.data(), 1, p.bytes.size(), fp_);
}

void pcap_file::flush() {
    if (fp_) std::fflush(fp_);
}

// ---------------------------------------------------------------------------
// producer coroutine
// ---------------------------------------------------------------------------
task<void> producer(io_context& ctx, thread_pool& pool, channel<packet>& ch,
                    pcap_file& pf, counters& st, mode how, unsigned disk_us,
                    std::mt19937& rng) {
    uint64_t seq = 0;
    while (!ctx.stopped()) {
        packet p;
        p.seq = seq++;
        p.bytes.resize(96 + rng() % 32);        // ~96-byte frames
        st.produced.fetch_add(1, std::memory_order_relaxed);

        switch (how) {
            case mode::sync_blocking:
                // Blocking write inline: the loop thread (and capture) stalls.
                pf.write(p, disk_us);
                st.written.fetch_add(1, std::memory_order_relaxed);
                break;
            case mode::thread_only:
                // Hand off to a pool thread, NO coroutine, NO waiting. While the
                // worker is busy with the disk, the loop thread keeps capturing.
                pool.run([&pf, &st, disk_us, p] {
                    pf.write(p, disk_us);
                    st.written.fetch_add(1, std::memory_order_relaxed);
                });
                break;
            case mode::coroutine:
                // Push to the channel; the writer coroutine will co_await a job.
                ch.push(std::move(p));
                break;
        }
        co_await sleep_for(ctx, 1);             // yield so other coroutines run
    }
}

// ---------------------------------------------------------------------------
// writer coroutine (coro mode only)
// ---------------------------------------------------------------------------
task<void> writer(io_context& ctx, thread_pool& pool, channel<packet>& ch,
                  pcap_file& pf, counters& st, unsigned disk_us) {
    for (;;) {
        packet p = co_await ch.pop();                        // suspend until data
        co_await pool.async(ctx, [&pf, disk_us, p] {
            pf.write(p, disk_us);                    // blocking write on a worker
        });
        st.written.fetch_add(1, std::memory_order_relaxed);
    }
}