// Chapter 7 — Memory Management
// Topic 8: Cache Lines, False Sharing, and Hardware Prefetch
// Compile: g++ -std=c++20 -O2 -pthread -o 08 08_cache_lines_false_sharing.cpp
// Run:     ./08

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <new>         // hardware_destructive_interference_size
#include <thread>
#include <vector>

static inline uint64_t rdtsc() {
#if defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return static_cast<uint64_t>(hi) << 32 | lo;
#else
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

template <typename T>
void sink(T const& v) { asm volatile("" : : "r,m"(v) : "memory"); }

static constexpr int ITER = 10'000'000;

// ─── 1. hardware_destructive_interference_size ────────────────────────────────

void show_interference_size() {
    std::cout << "--- Cache Line Constants ---\n";
#ifdef __cpp_lib_hardware_interference_size
    std::cout << "hardware_destructive_interference_size  = "
              << std::hardware_destructive_interference_size << "\n";
    std::cout << "hardware_constructive_interference_size = "
              << std::hardware_constructive_interference_size << "\n";
#else
    std::cout << "C++17 hardware interference size not available.\n";
    std::cout << "Fallback: x86 = 64 bytes, Apple Silicon M1/M2 = 128 bytes.\n";
#endif
    std::cout << "\n";
}

// ─── 2. False sharing benchmark ──────────────────────────────────────────────

// BAD: two counters on the same cache line
struct FalseShared {
    std::atomic<uint64_t> a{0};   // offset 0
    std::atomic<uint64_t> b{8};   // offset 8  ← same 64-byte cache line!
};
static_assert(sizeof(FalseShared) == 16);

// GOOD: each counter on its own cache line
struct PaddedCounter {
#ifdef __cpp_lib_hardware_interference_size
    alignas(std::hardware_destructive_interference_size)
#else
    alignas(64)
#endif
    std::atomic<uint64_t> value{0};
};

void bench_false_sharing() {
    std::cout << "--- False Sharing Benchmark (" << ITER << " iters/thread) ---\n";

    // --- BAD: false sharing ---
    FalseShared bad;
    auto t0 = std::chrono::steady_clock::now();
    {
        std::thread t1([&]{ for (int i = 0; i < ITER; ++i) bad.a.fetch_add(1, std::memory_order_relaxed); });
        std::thread t2([&]{ for (int i = 0; i < ITER; ++i) bad.b.fetch_add(1, std::memory_order_relaxed); });
        t1.join(); t2.join();
    }
    auto t1 = std::chrono::steady_clock::now();
    auto bad_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

    // --- GOOD: separate cache lines ---
    PaddedCounter good_a, good_b;
    auto t2_start = std::chrono::steady_clock::now();
    {
        std::thread t1([&]{ for (int i = 0; i < ITER; ++i) good_a.value.fetch_add(1, std::memory_order_relaxed); });
        std::thread t2([&]{ for (int i = 0; i < ITER; ++i) good_b.value.fetch_add(1, std::memory_order_relaxed); });
        t1.join(); t2.join();
    }
    auto t2_end = std::chrono::steady_clock::now();
    auto good_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t2_end - t2_start).count();

    std::cout << "  False shared:     " << bad_ns  / ITER << " ns/iter total\n";
    std::cout << "  Padded (no share):" << good_ns / ITER << " ns/iter total\n";
    std::cout << "  Speedup:          " << (double)bad_ns / good_ns << "×\n\n";
}

// ─── 3. Cache line alignment for HFT book ────────────────────────────────────

// Bid and ask prices are on the same cache line (pack them together — constructive)
// but each is separated from the order counters (destructive — they change at different rates)

struct alignas(64) BookPrices {
    std::atomic<int64_t> bid_price{0};  // 8 B  offset 0
    std::atomic<int64_t> ask_price{0};  // 8 B  offset 8
    std::atomic<int32_t> bid_size{0};   // 4 B  offset 16
    std::atomic<int32_t> ask_size{0};   // 4 B  offset 20
    // rest of line: reserved
    char _hot_pad[40];                  // pad to 64 bytes
};

struct alignas(64) BookStats {          // separate line for slow-path writes
    uint64_t total_orders{0};
    uint64_t total_cancels{0};
    uint64_t last_update_ns{0};
    char _cold_pad[40];
};

static_assert(sizeof(BookPrices) == 64);
static_assert(sizeof(BookStats)  == 64);

void show_book_layout() {
    std::cout << "--- Order Book Cache Line Layout ---\n";
    BookPrices prices;
    BookStats  stats;

    uintptr_t prices_addr = reinterpret_cast<uintptr_t>(&prices);
    uintptr_t stats_addr  = reinterpret_cast<uintptr_t>(&stats);

    std::cout << "BookPrices at " << &prices << " (line " << prices_addr / 64 << ")\n";
    std::cout << "BookStats  at " << &stats  << " (line " << stats_addr  / 64 << ")\n";
    std::cout << "Same cache line: "
              << ((prices_addr / 64) == (stats_addr / 64) ? "YES (BAD)" : "NO (GOOD)") << "\n";
    std::cout << "Market-data readers never load BookStats when reading bid/ask.\n\n";
}

// ─── 4. Prefetch demo ────────────────────────────────────────────────────────

static constexpr int PREFETCH_N = 10'000'000;

void bench_prefetch() {
    std::cout << "--- Hardware Prefetch Demo ---\n";

    // Sequential access — hardware prefetcher kicks in automatically
    std::vector<int64_t> data(PREFETCH_N);
    for (int i = 0; i < PREFETCH_N; ++i) data[i] = i;

    // Without manual prefetch (hardware prefetcher still helps for sequential):
    {
        int64_t sum = 0;
        uint64_t t0 = rdtsc();
        for (int i = 0; i < PREFETCH_N; ++i) sum += data[i];
        uint64_t cycles = rdtsc() - t0;
        sink(sum);
        std::cout << "  Sequential (hardware PF): " << cycles / PREFETCH_N << " cycles/iter\n";
    }

    // With manual software prefetch — can help for irregular patterns
    {
        int64_t sum = 0;
        uint64_t t0 = rdtsc();
        for (int i = 0; i < PREFETCH_N; ++i) {
            if (i + 32 < PREFETCH_N)
                __builtin_prefetch(&data[i + 32], 0, 1);  // prefetch 32 ahead, read, locality 1
            sum += data[i];
        }
        uint64_t cycles = rdtsc() - t0;
        sink(sum);
        std::cout << "  Sequential (software PF): " << cycles / PREFETCH_N << " cycles/iter\n";
    }

    // Pointer-chasing (defeats hardware prefetcher):
    {
        // Build a random permutation pointer chain
        std::vector<int64_t*> chain(PREFETCH_N);
        for (int i = 0; i < PREFETCH_N; ++i) chain[i] = &data[i];
        // Shuffle to create random access pattern
        for (int i = PREFETCH_N - 1; i > 0; --i) {
            int j = (i * 1664525 + 1013904223) % (i + 1);
            std::swap(chain[i], chain[j]);
        }

        int64_t sum = 0;
        constexpr int CHAIN_N = 1000;  // smaller for pointer-chase test
        uint64_t t0 = rdtsc();
        for (int i = 0; i < CHAIN_N; ++i) sum += *chain[i];
        uint64_t cycles = rdtsc() - t0;
        sink(sum);
        std::cout << "  Random ptr-chase:         " << cycles / CHAIN_N
                  << " cycles/iter (mostly cache misses)\n";
    }

    std::cout << "\n";
}

// ─── 5. Padding struct to prevent false sharing (pattern reference) ───────────

template <typename T>
struct alignas(64) CacheAligned {
    T value;
    // Implicit padding to 64 bytes (if sizeof(T) < 64)
};

void show_cache_aligned() {
    std::cout << "--- CacheAligned<T> ---\n";
    CacheAligned<std::atomic<int>> a, b, c;
    std::cout << "sizeof(CacheAligned<atomic<int>>) = " << sizeof(a) << "\n";
    std::cout << "&a=" << &a << " &b=" << &b << " &c=" << &c << "\n";
    std::cout << "Consecutive elements are 64 bytes apart — no false sharing.\n\n";
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Cache Lines, False Sharing, Prefetch ===\n\n";
    show_interference_size();
    bench_false_sharing();
    show_book_layout();
    bench_prefetch();
    show_cache_aligned();

    std::cout << "=== Rules ===\n"
              << "  1. hardware_destructive_interference_size: use instead of hardcoded 64.\n"
              << "     (Apple Silicon M1/M2 = 128 bytes!)\n"
              << "  2. False sharing: two threads write to the same line → 100-400 ns stall.\n"
              << "     Fix: alignas(64) + padding.\n"
              << "  3. Constructive sharing: co-accessed read-only data on the same line.\n"
              << "  4. Prefetch: __builtin_prefetch(addr, rw, locality). For regular patterns\n"
              << "     the hardware prefetcher is better. For irregular: software prefetch helps.\n"
              << "  5. Linked lists → random ptr chase → cache miss per node → ~70 ns each.\n"
              << "     Prefer vectors: hardware PF + stride-1 access → near-zero cache misses.\n";
    return 0;
}
