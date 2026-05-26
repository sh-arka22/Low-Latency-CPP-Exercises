/**
 * Chapter 3/4 — Measuring Performance + Cache Effects
 * Topic: Cache Hierarchy, Spatial & Temporal Locality, Cache Thrashing
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra 04_cache_effects.cpp -o cache_effects
 * Perf:  perf stat -e cache-misses,cache-references ./cache_effects
 *
 * Key ideas:
 *   - L1 cache: ~0.5 ns  |  L2: ~7 ns  |  Main memory: ~100 ns
 *   - Cache line = 64 bytes — fetching 1 byte fetches 64 bytes
 *   - Spatial locality: access nearby memory (same cache line) → fast
 *   - Temporal locality: reuse recently-accessed memory → fast
 *   - Cache thrashing: inner loop accesses memory strides > cache line → 20× slower
 */

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

using Clock = std::chrono::steady_clock;

template <typename F>
long long time_ms(F&& f) {
    auto t0 = Clock::now();
    f();
    auto t1 = Clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
}

// ─── SECTION 1: Cache Line basics ────────────────────────────────────────────
// Fetching 1 byte pulls 64 bytes from RAM. Reading adjacent elements is "free".

void section_cache_line_basics() {
    std::cout << "\n=== SECTION 1: Cache line basics ===\n";

    // On most x86_64 systems: cache line = 64 bytes
    std::cout << "Typical cache line: 64 bytes\n";
    std::cout << "L1 cache latency:   ~0.5 ns\n";
    std::cout << "L2 cache latency:   ~7 ns\n";
    std::cout << "Main memory:        ~100 ns\n";
    std::cout << "Ratio L1→RAM:       ~200×\n\n";

    // Accessing elements 0, 1, 2, ..., 15 of an int array = 1 cache line (64 bytes)
    alignas(64) std::array<int, 16> line{};
    std::iota(line.begin(), line.end(), 0);

    int sum = 0;
    for (auto v : line) sum += v;   // all 16 ints fit in 1 cache line = 1 fetch from RAM
    std::cout << "Sum of 16 ints (1 cache line): " << sum << "\n";
}

// ─── SECTION 2: Cache thrashing (from the book!) ─────────────────────────────
// Book example: matrix[i][j] = sequential → fast
//               matrix[j][i] = column-major → L1 cache miss on every access

constexpr int kL1CacheCapacity = 32768;   // typical L1d = 32 KB
constexpr int kMatSize = kL1CacheCapacity / sizeof(int);  // 8192

using MatrixType = std::array<std::array<int, kMatSize>, kMatSize>;

// WARNING: kMatSize=8192, so MatrixType is 8192*8192*4 = 256 MB!
// That's too large for the stack. We use a smaller demo instead.
// The PRINCIPLE is identical — just at a smaller scale.

constexpr int kDemoSize = 256;   // 256×256 ints = 256 KB (fits in L2)
using DemoMatrix = std::array<std::array<int, kDemoSize>, kDemoSize>;

void section_cache_thrashing() {
    std::cout << "\n=== SECTION 2: Cache thrashing ===\n";
    DemoMatrix matrix{};

    // Row-major access: matrix[i][j] — sequential memory → spatial locality
    // Each row is contiguous; we walk row by row → cache-friendly
    auto ms_row = time_ms([&]{
        int counter = 0;
        for (int rep = 0; rep < 100; ++rep)
            for (int i = 0; i < kDemoSize; ++i)
                for (int j = 0; j < kDemoSize; ++j)
                    matrix[i][j] = counter++;
    });

    // Column-major access: matrix[j][i] — stride = kDemoSize*4 bytes per step
    // Each step jumps to a different row → cache line evicted before reuse
    auto ms_col = time_ms([&]{
        int counter = 0;
        for (int rep = 0; rep < 100; ++rep)
            for (int i = 0; i < kDemoSize; ++i)
                for (int j = 0; j < kDemoSize; ++j)
                    matrix[j][i] = counter++;   // ← only change!
    });

    std::cout << "Row-major (cache-friendly):   " << ms_row << " ms\n";
    std::cout << "Col-major (cache-thrashing):  " << ms_col << " ms\n";
    if (ms_row > 0)
        std::cout << "Slowdown factor: " << (double)ms_col / ms_row << "×\n";

    // BOOK RESULT: 40 ms (row) vs 800 ms (col) = 20× slowdown
    // YOUR RESULT: will scale with matrix size and cache sizes
}

// ─── SECTION 3: Spatial locality — vector vs pointer-chasing ─────────────────
// Ch1 insight: vector<T> = 1 allocation, contiguous → cache-friendly
// pointer-based list = each node allocated separately → cache miss per element

struct Node {
    int val;
    Node* next{nullptr};
};

void section_spatial_locality() {
    std::cout << "\n=== SECTION 3: Spatial locality — vector vs linked list ===\n";
    constexpr int N = 1'000'000;

    // Vector: all elements contiguous in memory
    std::vector<int> vec(N);
    std::iota(vec.begin(), vec.end(), 0);

    auto ms_vec = time_ms([&]{
        volatile long sum = 0;
        for (int v : vec) sum += v;
    });

    // Linked list: each node is a separate heap allocation (random memory)
    std::vector<Node> pool(N);   // use pool to avoid fragmentation killing the demo
    for (int i = 0; i < N-1; ++i) pool[i].next = &pool[i+1];
    for (int i = 0; i < N; ++i)   pool[i].val  = i;

    auto ms_list = time_ms([&]{
        volatile long sum = 0;
        for (Node* n = &pool[0]; n; n = n->next) sum += n->val;
    });

    std::cout << "vector<int> traversal:       " << ms_vec  << " ms\n";
    std::cout << "linked list traversal (pool): " << ms_list << " ms\n";
    std::cout << "(Pool reduces fragmentation. Real separate-alloc list is much worse.)\n";

    // HFT LESSON: an order book implemented as a vector of price levels
    // traverses 10-50× faster than a linked list. This is not a micro-opt — it's
    // the difference between matching in 100 ns vs 2000 ns.
}

// ─── SECTION 4: Temporal locality ────────────────────────────────────────────
// Reusing the same memory repeatedly → it stays in L1/L2 cache

void section_temporal_locality() {
    std::cout << "\n=== SECTION 4: Temporal locality ===\n";
    constexpr int N = 1024;   // fits in L1 cache (1024 ints = 4 KB < 32 KB L1)
    constexpr int LARGE = 1'000'000;  // 4 MB — larger than L2, won't fit in cache

    std::vector<int> small_buf(N, 1);
    std::vector<int> large_buf(LARGE, 1);
    constexpr int REPS = 10'000;

    // Repeatedly sum a small buffer — stays in L1 after first access
    auto ms_small = time_ms([&]{
        volatile long sum = 0;
        for (int r = 0; r < REPS; ++r)
            for (int v : small_buf) sum += v;
    });

    // Repeatedly sum a large buffer — too big for L2/L3 on many CPUs
    auto ms_large = time_ms([&]{
        volatile long sum = 0;
        for (int r = 0; r < REPS; ++r)
            for (int v : large_buf) sum += v;
    });

    std::cout << "Small buffer (" << N      << " ints, ~fits L1): " << ms_small << " ms\n";
    std::cout << "Large buffer (" << LARGE  << " ints, exceeds cache): " << ms_large << " ms\n";
    // Per element: large >> small because cache miss on every access
}

// ─── SECTION 5: SoA vs AoS — HFT pattern ────────────────────────────────────
// Array of Structs (AoS): struct {bid, ask, size} → fields interleaved
// Struct of Arrays (SoA): {bids[], asks[], sizes[]} → fields separated
//
// If you only need to scan `bid` prices, SoA is faster — only bid[] is loaded.

struct PriceLevelAoS {
    double bid;
    double ask;
    int    size;
    int    padding;   // align to 16 bytes
};

struct PriceLevelsSoA {
    std::vector<double> bids;
    std::vector<double> asks;
    std::vector<int>    sizes;
};

void section_soa_vs_aos() {
    std::cout << "\n=== SECTION 5: SoA vs AoS ===\n";
    constexpr int N = 1'000'000;

    // AoS: scan only bids — but ask and size come along for the ride (cache pollution)
    std::vector<PriceLevelAoS> aos(N);
    for (int i = 0; i < N; ++i) aos[i].bid = i * 0.01;

    // SoA: only bids are in memory — tight packing, no wasted cache lines
    PriceLevelsSoA soa;
    soa.bids.resize(N);
    soa.asks.resize(N);
    soa.sizes.resize(N);
    for (int i = 0; i < N; ++i) soa.bids[i] = i * 0.01;

    auto ms_aos = time_ms([&]{
        volatile double sum = 0;
        for (const auto& pl : aos) sum += pl.bid;  // loads 16 bytes per bid, wastes 12
    });

    auto ms_soa = time_ms([&]{
        volatile double sum = 0;
        for (double b : soa.bids) sum += b;         // loads 8 bytes per bid, wastes 0
    });

    std::cout << "AoS bid scan (N=" << N << "): " << ms_aos << " ms\n";
    std::cout << "SoA bid scan (N=" << N << "): " << ms_soa << " ms\n";
    std::cout << "SoA speedup: " << (double)ms_aos / std::max(ms_soa, 1LL) << "×\n";
    std::cout << "Lesson: SoA wins when hot loops touch only 1-2 fields per element.\n";
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    section_cache_line_basics();
    section_cache_thrashing();
    section_spatial_locality();
    section_temporal_locality();
    section_soa_vs_aos();

    std::cout << "\n✓ All cache effect sections complete.\n";
    std::cout << "Run with 'perf stat -e cache-misses,cache-references ./cache_effects' "
                 "to see hardware counters.\n";
    return 0;
}
