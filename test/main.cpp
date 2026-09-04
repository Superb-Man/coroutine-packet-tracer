// main.cpp - The CLI driver. All the coroutine logic lives in tracer.hpp/cpp;
// this file only parses flags, sets up the io_context + one shared thread pool,
// and prints the comparison.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>

#include "tracer.hpp"

static void usage(const char* prog) {
    std::fprintf(stderr,
        "usage: %s [--mode sync|thread|coro|all] [--disk-us N] [--pool N] "
        "[--win-sec S] [--out FILE]\n"
        "  --mode sync  : blocking write INLINE on the loop thread (slow, stalls capture)\n"
        "         thread : hand each write to a pool thread (no coroutines)\n"
        "         coro   : writer coroutine co-awaits a pool write (no drop during I/O)\n"
        "         all    : run sync, thread, and coro sequentially for comparison\n"
        "  --disk-us N  : simulated microseconds per disk write (default 5000)\n"
        "  --pool N     : worker threads (shared by thread & coro modes; default 4)\n"
        "  --win-sec S  : capture window in seconds (default 5)\n"
        "  --out FILE   : pcap output file (default out.pcap)\n", prog);
}

static const char* mode_name(mode how) {
    switch (how) {
        case mode::sync_blocking: return "sync";
        case mode::thread_only:   return "thread";
        case mode::coroutine:     return "coro";
    }
    return "unknown";
}

static std::string output_for_mode(
    const std::string& path,
    const char* selected_mode) {
    auto slash = path.find_last_of("/\\");
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos ||
        (slash != std::string::npos && dot < slash)) {
        return path + "-" + selected_mode + ".pcap";
    }
    return path.substr(0, dot) + "-" + selected_mode + path.substr(dot);
}

static void run_mode(mode how, unsigned disk_us, unsigned pool_n,
                     unsigned win_sec, const std::string& out) {
    io_context ctx;
    thread_pool pool(pool_n);      // ONE pool used by thread-only & coro
    channel<packet> ch;            // used only by coro mode
    pcap_file pf(out);
    counters st;
    std::mt19937 rng(42);          // same packet sequence for every mode

    // Coroutines are eager: creating them starts them immediately; ctx.run()
    // drives them until told to stop. In coro mode the writer coroutine does
    // the co_await; in thread/sync modes there is no writer coroutine.
    auto prod = producer(ctx, pool, ch, pf, st, how, disk_us, rng);
    task<void> wrt;
    if (how == mode::coroutine) {
        wrt = writer(ctx, pool, ch, pf, st, disk_us);
    }
    (void)prod;
    (void)wrt;

    std::fprintf(stderr,
                 "mode=%s  disk=%dus/write  pool(writer threads)=%d  window=%us\n",
                 mode_name(how), disk_us, pool_n, win_sec);

    auto start = std::chrono::steady_clock::now();
    std::thread looper([&] { ctx.run(); });

    std::this_thread::sleep_for(std::chrono::seconds(win_sec));
    ctx.stop();
    looper.join();

    pool.wait_idle();   // drain in-flight wraps so all dispatched writes finish

    // coro mode: a few packets may still be sitting in the channel — write them
    // out so nothing captured is lost (sync/thread modes have no backlog).
    if (how == mode::coroutine) {
        packet leftover;
        while (ch.try_pop(leftover)) {
            pf.write(leftover);
            st.written.fetch_add(1);
        }
    }
    pf.flush();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - start).count();
    uint64_t produced = st.produced.load();
    uint64_t written = st.written.load();
    double capture_rate = ms == 0
        ? 0.0
        : 1000.0 * static_cast<double>(produced) / static_cast<double>(ms);

    std::fprintf(stderr, "  in %lld ms: produced=%llu  written=%llu\n",
                 static_cast<long long>(ms),
                 static_cast<unsigned long long>(produced),
                 static_cast<unsigned long long>(written));
    std::fprintf(stderr, "  capture_rate=%.1f pkts/s\n", capture_rate);

    switch (how) {
        case mode::sync_blocking:
            std::fprintf(stderr,
                "  (sync: capture was blocked by each synchronous disk write)\n");
            break;
        case mode::thread_only:
            std::fprintf(stderr,
                "  (thread: capture never blocked; a worker does the write, no coroutine)\n");
            break;
        case mode::coroutine:
            std::fprintf(stderr,
                "  (coro: writer coroutine co-awaits the same pool; loop keeps capturing)\n");
            break;
    }
    std::fprintf(stderr, "  wrote pcap file: %s\n", out.c_str());
}

int main(int argc, char** argv) {
    mode how = mode::coroutine;
    bool run_all = false;
    unsigned disk_us = 5000;
    unsigned pool_n = 4;
    unsigned win_sec = 5;
    std::string out = "out.pcap";

    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        auto value = [&]() -> std::string {
            return (i + 1 < argc) ? argv[++i] : "";
        };

        if (argument == "--mode") {
            std::string selected = value();
            run_all = selected == "all";
            if (selected == "sync")       how = mode::sync_blocking;
            else if (selected == "thread") how = mode::thread_only;
            else if (selected == "coro")  how = mode::coroutine;
            else if (!run_all) { usage(argv[0]); return 2; }
        } else if (argument == "--disk-us") {
            disk_us = static_cast<unsigned>(std::stoul(value()));
        } else if (argument == "--pool") {
            pool_n = static_cast<unsigned>(std::stoul(value()));
        } else if (argument == "--win-sec") {
            win_sec = static_cast<unsigned>(std::stoul(value()));
        } else if (argument == "--out") {
            out = value();
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (run_all) {
        const mode modes[] = {
            mode::sync_blocking,
            mode::thread_only,
            mode::coroutine
        };
        for (mode selected : modes) {
            std::fprintf(stderr, "\n=== %s mode ===\n", mode_name(selected));
            run_mode(selected, disk_us, pool_n, win_sec,
                     output_for_mode(out, mode_name(selected)));
        }
    } else {
        run_mode(how, disk_us, pool_n, win_sec, out);
    }

    return 0;
}
