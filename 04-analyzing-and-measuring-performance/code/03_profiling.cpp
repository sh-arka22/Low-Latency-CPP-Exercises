/**
 * Chapter 3 — Measuring Performance
 * Topics 3-5: Performance Properties, Instrumentation Profilers, Sampling Profilers
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra 03_profiling.cpp -o profiling
 * Build with gprof: g++ -std=c++20 -O2 -pg 03_profiling.cpp -o profiling_gprof
 *   Then run: ./profiling_gprof && gprof profiling_gprof gmon.out | less
 *
 * Key ideas:
 *   - Use steady_clock (monotonic) for timing, NOT system_clock
 *   - Instrumentation profilers: exact but add overhead
 *   - Sampling profilers: approximate but low overhead; sleeping threads invisible
 *   - Self% = hot function; Total% = frequently on call stack
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif

// ─── SECTION 1: ScopedTimer (book's instrumentation profiler) ───────────────

class ScopedTimer {
public:
    using ClockType = std::chrono::steady_clock;

    explicit ScopedTimer(const char* func)
        : function_{func}, start_{ClockType::now()} {}

    ScopedTimer(const ScopedTimer&)            = delete;
    ScopedTimer(ScopedTimer&&)                 = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
    ScopedTimer& operator=(ScopedTimer&&)      = delete;

    ~ScopedTimer() {
        using namespace std::chrono;
        auto stop     = ClockType::now();
        auto duration = duration_cast<nanoseconds>(stop - start_);
        std::cout << "[ScopedTimer] " << function_
                  << " took " << duration.count() << " ns\n";
    }

private:
    const char*               function_;
    const ClockType::time_point start_;
};

// Macro: zero cost when USE_TIMER is not defined
#ifdef USE_TIMER
    #define MEASURE_FUNCTION() ScopedTimer _timer_{__func__}
#else
    #define MEASURE_FUNCTION()
#endif

// ─── WHY steady_clock and NOT system_clock? ──────────────────────────────────
//
//   system_clock: wall-clock time; can jump forward/backward (NTP adjustments).
//   steady_clock: monotonically increasing; designed for measuring intervals.
//   If NTP adjusts the clock mid-measurement, system_clock gives wrong deltas.
//
// Example function demonstrating correct timing:
void timed_work() {
    MEASURE_FUNCTION();                   // only active if -DUSE_TIMER
    volatile int sum = 0;
    for (int i = 0; i < 1'000'000; ++i) sum += i;
    (void)sum;
}

void section_scoped_timer() {
    std::cout << "\n=== SECTION 1: ScopedTimer ===\n";

    // Direct usage (always on):
    {
        ScopedTimer t{"manual_block"};
        volatile long sum = 0;
        for (int i = 0; i < 5'000'000; ++i) sum += i;
        (void)sum;
    }

    timed_work();   // MEASURE_FUNCTION() fires only if -DUSE_TIMER
    std::cout << "(Rebuild with -DUSE_TIMER to see MEASURE_FUNCTION() output)\n";
}

// ─── SECTION 2: Latency statistics (percentiles) ────────────────────────────
// Mean = easy; median = robust; p99 = reveals tail latency (critical in HFT)

struct LatencyStats {
    long long min_ns, max_ns, mean_ns, median_ns, p99_ns;
};

LatencyStats compute_stats(std::vector<long long> samples) {
    std::sort(samples.begin(), samples.end());
    size_t n = samples.size();
    long long sum = 0;
    for (auto s : samples) sum += s;
    return {
        .min_ns    = samples.front(),
        .max_ns    = samples.back(),
        .mean_ns   = sum / (long long)n,
        .median_ns = samples[n / 2],
        .p99_ns    = samples[(size_t)(n * 0.99)]
    };
}

void measure_latency(auto&& fn, int iterations, const char* label) {
    std::vector<long long> samples(iterations);
    for (int i = 0; i < iterations; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        fn();
        auto t1 = std::chrono::steady_clock::now();
        samples[i] = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    }
    auto s = compute_stats(std::move(samples));
    std::cout << label << " (" << iterations << " iters):\n"
              << "  min=" << s.min_ns << " ns  mean=" << s.mean_ns << " ns"
              << "  median=" << s.median_ns << " ns  p99=" << s.p99_ns
              << " ns  max=" << s.max_ns << " ns\n";
}

void section_latency_stats() {
    std::cout << "\n=== SECTION 2: Latency statistics ===\n";

    std::vector<int> data(10'000);
    std::iota(data.rbegin(), data.rend(), 0);   // reverse sorted — worst case

    measure_latency([&]{
        auto v = data;   // copy, then sort
        std::sort(v.begin(), v.end());
    }, 1000, "sort(10k ints)");

    // KEY INSIGHT: p99 >> median is a sign of occasional cache eviction or
    // OS scheduling interference. In HFT, p99 latency can destroy a strategy.
}

// ─── SECTION 3: Sampling profiler simulation ─────────────────────────────────
// Demonstrates what Self% and Total% mean conceptually

// These functions represent a typical call tree: main → f1 → f2 → f3
// f3 has the highest "Self%" — it's where the CPU spends most real time

void f3_hot() {
    // Expensive computation — this is the "hot" function
    volatile long sum = 0;
    for (int i = 0; i < 5'000'000; ++i) sum ^= i;   // XOR prevents optimization
    (void)sum;
}

void f2_medium() {
    // Does some work, then calls f3
    volatile int x = 0;
    for (int i = 0; i < 100'000; ++i) x += i;
    (void)x;
    f3_hot();   // f3 is on the call stack whenever f2 is
}

void f1_light() {
    // Almost no own work — mostly a wrapper
    f2_medium();
}

void section_sampling_profiler_simulation() {
    std::cout << "\n=== SECTION 3: Sampling profiler simulation ===\n";
    std::cout << "Run with gprof to see call graph. Expected profile:\n";
    std::cout << "  f3_hot()    — highest Self% (does most actual work)\n";
    std::cout << "  f2_medium() — high Total% (on stack whenever f3 runs)\n";
    std::cout << "  f1_light()  — high Total%, low Self% (wrapper only)\n\n";

    ScopedTimer t{"f1_light"};
    f1_light();
}

// ─── SECTION 4: Thread sleeping invisible to sampling profilers ───────────────
// KEY INSIGHT: a mutex-blocked thread shows 0% in sampling profile
// because the thread is SLEEPING, not running on CPU

void section_sleeping_thread_invisibility() {
    std::cout << "\n=== SECTION 4: Sleeping threads invisible to sampling profilers ===\n";

    std::atomic<bool> done{false};

    // This thread spins (visible to profiler):
    auto cpu_bound = std::thread([&]{
        long sum = 0;
        while (!done.load(std::memory_order_relaxed)) sum++;
        (void)sum;
    });

    // This thread sleeps (invisible to sampling profiler):
    auto io_bound = std::thread([&]{
        while (!done.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    done.store(true);
    cpu_bound.join();
    io_bound.join();

    std::cout << "Ran for ~50ms. A sampling profiler would show:\n";
    std::cout << "  cpu_bound thread: ~100% Self% (spinning on CPU)\n";
    std::cout << "  io_bound thread:  ~0%   Self% (sleeping — not scheduled)\n";
    std::cout << "Lesson: lock contention / I/O waits are INVISIBLE in sampling profiles.\n";
    std::cout << "Use perf sched / BPF tracing / Valgrind/Callgrind for those.\n";
}

// ─── SECTION 5: Pareto principle — find the hot 20% ─────────────────────────

void fast_fn()   { volatile int x = 1 + 1; (void)x; }
void medium_fn() { volatile long s = 0; for(int i=0;i<100'000;++i) s+=i; (void)s; }
void slow_fn()   { volatile long s = 0; for(int i=0;i<5'000'000;++i) s+=i; (void)s; }

void section_pareto() {
    std::cout << "\n=== SECTION 5: Pareto Principle (80/20 rule) ===\n";

    auto t_fast   = ScopedTimer{"fast_fn"};   (void)t_fast;
    fast_fn();
    // re-measure manually:
    auto [a, b, c] = []{
        auto t0 = std::chrono::steady_clock::now;
        auto start = t0();
        fast_fn(); auto t1 = t0();
        medium_fn(); auto t2 = t0();
        slow_fn(); auto t3 = t0();
        using ns = std::chrono::nanoseconds;
        return std::tuple{
            std::chrono::duration_cast<ns>(t1-start).count(),
            std::chrono::duration_cast<ns>(t2-t1).count(),
            std::chrono::duration_cast<ns>(t3-t2).count()
        };
    }();
    long long total = a + b + c;
    std::cout << "fast_fn:   " << a << " ns (" << (100*a/total) << "%)\n";
    std::cout << "medium_fn: " << b << " ns (" << (100*b/total) << "%)\n";
    std::cout << "slow_fn:   " << c << " ns (" << (100*c/total) << "%)\n";
    std::cout << "→ Optimize slow_fn first. It is the 20% driving 80% of time.\n";
}

// ─── SECTION 6: NanoTimer & LatencyHistogram (HFT Challenge) ────────────────

inline uint64_t get_cycles() {
#if defined(__x86_64__) || defined(__i386__)
    return __rdtsc();
#elif defined(__aarch64__)
    uint64_t tsc;
    asm volatile("mrs %0, cntvct_el0" : "=r" (tsc));
    return tsc;
#else
    return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
}

double calibrate_tsc_to_ns() {
    auto t0 = std::chrono::steady_clock::now();
    uint64_t c0 = get_cycles();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    uint64_t c1 = get_cycles();
    auto t1 = std::chrono::steady_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    return (double)ns / (c1 - c0);
}

template <size_t N>
class LatencyHistogram {
    std::array<uint64_t, N> buckets_{};
    uint64_t bin_width_ns_;
    uint64_t total_samples_ = 0;
public:
    explicit LatencyHistogram(uint64_t bin_width_ns = 10) : bin_width_ns_(bin_width_ns) {}

    void record(uint64_t latency_ns) {
        size_t bin = latency_ns / bin_width_ns_;
        if (bin >= N) bin = N - 1; // Overflow bin
        buckets_[bin]++;
        total_samples_++;
    }

    void report() const {
        std::cout << "[LatencyHistogram] Total Samples: " << total_samples_ << "\n";
        if (total_samples_ == 0) return;
        
        uint64_t count = 0;
        uint64_t p50_idx = total_samples_ * 0.50;
        uint64_t p95_idx = total_samples_ * 0.95;
        uint64_t p99_idx = total_samples_ * 0.99;
        uint64_t p999_idx = total_samples_ * 0.999;
        
        uint64_t p50=0, p95=0, p99=0, p999=0;
        
        for (size_t i = 0; i < N; ++i) {
            count += buckets_[i];
            if (!p50 && count >= p50_idx) p50 = i * bin_width_ns_;
            if (!p95 && count >= p95_idx) p95 = i * bin_width_ns_;
            if (!p99 && count >= p99_idx) p99 = i * bin_width_ns_;
            if (!p999 && count >= p999_idx) p999 = i * bin_width_ns_;
        }
        
        std::cout << "  p50: ~" << p50 << " ns\n";
        std::cout << "  p95: ~" << p95 << " ns\n";
        std::cout << "  p99: ~" << p99 << " ns\n";
        std::cout << " p999: ~" << p999 << " ns\n";
    }
};

class NanoTimer {
    uint64_t start_tsc_;
    uint64_t* out_ns_;
    double tsc_to_ns_;

public:
    explicit NanoTimer(uint64_t* out_ns, double tsc_to_ns) 
        : out_ns_(out_ns), tsc_to_ns_(tsc_to_ns) {
        start_tsc_ = get_cycles();
    }
    ~NanoTimer() {
        uint64_t end_tsc = get_cycles();
        if (out_ns_) {
            *out_ns_ = static_cast<uint64_t>((end_tsc - start_tsc_) * tsc_to_ns_);
        }
    }
};

void section_hft_profiling() {
    std::cout << "\n=== SECTION 6: HFT Profiling (NanoTimer & Histogram) ===\n";
    double tsc_to_ns = calibrate_tsc_to_ns();
    std::cout << "Calibrated TSC to ns ratio: " << tsc_to_ns << "\n";
    
    LatencyHistogram<1000> hist(10); // 10ns bins up to 10us
    
    // Test NanoTimer overhead
    uint64_t overhead_ns = 0;
    {
        NanoTimer t(&overhead_ns, tsc_to_ns);
    }
    std::cout << "NanoTimer overhead: " << overhead_ns << " ns\n";
    
    // Simulate 10k lookups
    for (int i = 0; i < 10000; ++i) {
        uint64_t latency = 0;
        {
            NanoTimer t(&latency, tsc_to_ns);
            // simulated work
            volatile int x = 0;
            for(int j=0; j<50; ++j) x += j;
        }
        hist.record(latency);
    }
    hist.report();
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    section_scoped_timer();
    section_latency_stats();
    section_sampling_profiler_simulation();
    section_sleeping_thread_invisibility();
    section_pareto();
    section_hft_profiling();

    std::cout << "\n✓ All profiling sections complete.\n";
    return 0;
}
