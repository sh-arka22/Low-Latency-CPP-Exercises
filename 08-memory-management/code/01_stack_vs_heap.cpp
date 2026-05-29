// Chapter 7 — Memory Management
// Topic 1: Stack vs Heap — allocation cost and fragmentation
// Compile: g++ -std=c++20 -O2 -o 01 01_stack_vs_heap.cpp
// Run:     ./01

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

// ─── utilities ────────────────────────────────────────────────────────────────

static inline uint64_t rdtsc() {
#if defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return static_cast<uint64_t>(hi) << 32 | lo;
#endif
}

// Prevent the compiler from eliding a variable (keeps it "alive")
template <typename T>
__attribute__((noinline)) void do_not_optimize(T const& val) {
    asm volatile("" : : "r,m"(val) : "memory");
}

// ─── benchmark helpers ────────────────────────────────────────────────────────

static constexpr int ITERATIONS = 1'000'000;

struct SmallObj {
    int64_t a, b, c, d;    // 32 bytes
};

// ─── 1. Stack allocation cost ─────────────────────────────────────────────────

void bench_stack() {
    uint64_t start = rdtsc();
    for (int i = 0; i < ITERATIONS; ++i) {
        SmallObj obj;           // sub rsp, 32  — one instruction
        obj.a = i; obj.b = i;
        do_not_optimize(obj.a);
    }
    uint64_t elapsed = rdtsc() - start;
    std::cout << "[Stack alloc]  cycles/iter: "
              << elapsed / ITERATIONS << "\n";
}

// ─── 2. Heap allocation cost (glibc malloc) ───────────────────────────────────

void bench_heap_malloc() {
    uint64_t start = rdtsc();
    for (int i = 0; i < ITERATIONS; ++i) {
        auto* p = static_cast<SmallObj*>(std::malloc(sizeof(SmallObj)));
        p->a = i;
        do_not_optimize(p->a);
        std::free(p);
    }
    uint64_t elapsed = rdtsc() - start;
    std::cout << "[Heap malloc]  cycles/iter: "
              << elapsed / ITERATIONS << "\n";
}

// ─── 3. Heap allocation cost (new/delete) ─────────────────────────────────────

void bench_heap_new() {
    uint64_t start = rdtsc();
    for (int i = 0; i < ITERATIONS; ++i) {
        auto* p = new SmallObj{};
        p->a = i;
        do_not_optimize(p->a);
        delete p;
    }
    uint64_t elapsed = rdtsc() - start;
    std::cout << "[Heap new]     cycles/iter: "
              << elapsed / ITERATIONS << "\n";
}

// ─── 4. Pre-allocated pool (reuse heap, never free in loop) ──────────────────

void bench_preallocated() {
    // Allocate once outside the timed section:
    std::vector<SmallObj> pool(ITERATIONS);
    uint64_t start = rdtsc();
    for (int i = 0; i < ITERATIONS; ++i) {
        SmallObj& obj = pool[i];   // no alloc — already exists
        obj.a = i;
        do_not_optimize(obj.a);
    }
    uint64_t elapsed = rdtsc() - start;
    std::cout << "[Pre-alloc]    cycles/iter: "
              << elapsed / ITERATIONS << "\n";
}

// ─── 5. sizeof and layout demo ────────────────────────────────────────────────

void show_sizeof() {
    std::cout << "\n--- sizeof demo ---\n";
    std::cout << "SmallObj on stack: " << sizeof(SmallObj) << " bytes\n";

    // Stack: the variable IS the object
    SmallObj stack_obj;
    std::cout << "stack_obj address:       " << &stack_obj << "\n";
    std::cout << "  (this is a stack addr, inside the current frame)\n";

    // Heap: the variable is a POINTER to the object
    SmallObj* heap_obj = new SmallObj{};
    std::cout << "heap_obj address:        " << heap_obj << "\n";
    std::cout << "heap_obj pointer itself: " << static_cast<void*>(&heap_obj)
              << " (stored on the stack!)\n";
    std::cout << "  Note: dereferencing heap_obj costs 1 cache miss if cold.\n";
    delete heap_obj;
}

// ─── 6. Fragmentation demo ────────────────────────────────────────────────────

void show_fragmentation() {
    std::cout << "\n--- Fragmentation demo ---\n";
    // Allocate objects of alternating sizes, then free half.
    // The freed slots are NOT re-usable for larger objects → external fragmentation.
    constexpr int N = 20;
    void* ptrs[N];

    for (int i = 0; i < N; ++i)
        ptrs[i] = std::malloc(i % 2 == 0 ? 16 : 64);

    // Free every other allocation (the 16-byte ones)
    for (int i = 0; i < N; i += 2)
        std::free(ptrs[i]);

    // Now try to allocate a 48-byte object: malloc must find a suitable free slot.
    // The 16-byte holes cannot service a 48-byte request → external fragmentation.
    void* large = std::malloc(48);
    std::cout << "After fragmenting, 48-byte alloc succeeded at: " << large << "\n";
    std::cout << "In production: fragmentation causes non-deterministic malloc latency.\n";

    // Cleanup
    for (int i = 1; i < N; i += 2) std::free(ptrs[i]);
    std::free(large);
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Stack vs Heap Allocation (" << ITERATIONS << " iters) ===\n\n";
    bench_stack();
    bench_heap_malloc();
    bench_heap_new();
    bench_preallocated();
    show_sizeof();
    show_fragmentation();

    std::cout << "\nKey takeaways:\n"
              << "  - Stack: ~0.3 ns (one instruction: sub rsp, N)\n"
              << "  - Heap:  ~50-500 ns (free list search + bookkeeping)\n"
              << "  - Pre-allocated pool: same as stack (no alloc in loop)\n"
              << "  - HFT rule: allocate everything at startup, zero malloc on hot path\n";
    return 0;
}
