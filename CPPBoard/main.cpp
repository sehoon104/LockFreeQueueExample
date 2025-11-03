#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include "Buffers.h"


#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#include <immintrin.h>
static inline void cpu_relax() noexcept { _mm_pause(); }
#else
static inline void cpu_relax() noexcept { std::this_thread::yield(); }
#endif

struct Result {
    double seconds{};
    uint64_t produced{};
    uint64_t consumed{};
    double throughput_mps{}; // consumed / s (million items/sec)
};

template <typename Push, typename Pop>
Result run_spsc_bench(Push push, Pop pop, std::chrono::seconds dur) {
    std::atomic<bool> stop{ false };
    std::atomic<uint64_t> produced{ 0 }, consumed{ 0 };

    // Consumer thread: spin-drain with light backoff
    std::thread consumer([&] {
        uint64_t v;
        while (!stop.load(std::memory_order_acquire)) {
            size_t got = 0;
            while (pop(v)) { ++got; }
            if (got == 0) cpu_relax();
            consumed.fetch_add(got, std::memory_order_relaxed);
        }
        // Grace drain a bit
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        size_t got = 0;
        do { while (pop(v)) ++got; } while (std::chrono::steady_clock::now() < until);
        consumed.fetch_add(got, std::memory_order_relaxed);
        });

    // Producer (single): flat-out with light backoff on full
    std::thread producer([&] {
        uint64_t x = 0;
        while (!stop.load(std::memory_order_acquire)) {
            if (push(x)) {
                ++x;
                produced.fetch_add(1, std::memory_order_relaxed);
            }
            else {
                for (int i = 0; i < 64; ++i) cpu_relax();
            }
        }
        });

    auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(dur);
    stop.store(true, std::memory_order_release);
    producer.join();
    consumer.join();
    auto t1 = std::chrono::steady_clock::now();

    double sec = std::chrono::duration_cast<std::chrono::duration<double>>(t1 - t0).count();
    uint64_t cons = consumed.load(std::memory_order_relaxed);
    return { sec, produced.load(std::memory_order_relaxed), cons, (cons / 1e6) / sec };
}

int main() {
    using namespace std::chrono;

    const size_t capacity = 1 << 20; // biggish ring to minimize backpressure
    const std::chrono::seconds bench_time = std::chrono::seconds(10);

    std::cout << "Running Lock-Free SPSC Queue benchmark, capacity=" << capacity
        << ", time=" << bench_time.count() << "s\n";

	// Lock-free SPSC queue
    SPSCQueue<uint64_t> lf(capacity);
    auto lfPush = [&](uint64_t v) { return lf.try_push(v); };
    auto lfPop = [&](uint64_t& o) { return lf.try_pop(o); };
    auto resLF = run_spsc_bench(lfPush, lfPop, bench_time);

    std::cout << "Running Mutex-SPSC Queue benchmark, capacity=" << capacity
        << ", time=" << bench_time.count() << "s\n";

	// Mutex-based bounded queue
    MutexBoundedQueue<uint64_t> mq(capacity);
    auto mPush = [&](uint64_t v) { return mq.try_push(v); };
    auto mPop = [&](uint64_t& o) { return mq.try_pop(o); };
    auto resMQ = run_spsc_bench(mPush, mPop, bench_time);

	// Print results
    auto print = [](const char* name, const Result& r) {
        std::cout << name << ":\n"
            << "  time:       " << r.seconds << " s\n"
            << "  produced:   " << r.produced << "\n"
            << "  consumed:   " << r.consumed << "\n"
            << "  throughput: " << r.throughput_mps << " M items/s\n";
        };

    std::cout << "\n=== Results ===\n";
    print("Lock-free SPSC", resLF);
    print("Mutex SPSC", resMQ);

    return 0;
}