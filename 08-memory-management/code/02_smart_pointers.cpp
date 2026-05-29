// Chapter 7 — Memory Management
// Topic 2: Smart Pointers — ownership, sizeof, cost
// Compile: g++ -std=c++20 -O2 -o 02 02_smart_pointers.cpp
// Run:     ./02

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

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
__attribute__((noinline)) void sink(T const& v) {
    asm volatile("" : : "r,m"(v) : "memory");
}

static constexpr int N = 1'000'000;

// ─── 1. sizeof each smart pointer ─────────────────────────────────────────────

struct Order {
    int64_t price;
    int32_t qty;
    int32_t id;
};

void show_sizeof() {
    std::cout << "--- sizeof ---\n";
    std::cout << "sizeof(Order)           = " << sizeof(Order) << "\n";
    std::cout << "sizeof(Order*)          = " << sizeof(Order*) << " (raw pointer)\n";
    std::cout << "sizeof(unique_ptr<Order>) = " << sizeof(std::unique_ptr<Order>) << "\n";
    std::cout << "sizeof(shared_ptr<Order>) = " << sizeof(std::shared_ptr<Order>) << "\n";
    std::cout << "sizeof(weak_ptr<Order>)   = " << sizeof(std::weak_ptr<Order>) << "\n";
    std::cout << "\n";
    // unique_ptr with default deleter == sizeof(T*) due to empty-base opt
    // shared_ptr always 16 bytes: ptr + ctrl_blk pointer
}

// ─── 2. unique_ptr — move cost ────────────────────────────────────────────────

void bench_unique_ptr_move() {
    auto p = std::make_unique<Order>(Order{100, 10, 1});
    uint64_t start = rdtsc();
    for (int i = 0; i < N; ++i) {
        auto q = std::move(p);   // pointer swap: ~0.3 ns
        sink(q.get());
        p = std::move(q);        // swap back
    }
    uint64_t elapsed = rdtsc() - start;
    std::cout << "[unique_ptr move]  cycles/iter: " << elapsed / N << "\n";
}

// ─── 3. shared_ptr — copy (atomic ref-count bump) ────────────────────────────

void bench_shared_ptr_copy() {
    auto p = std::make_shared<Order>(Order{100, 10, 1});
    uint64_t start = rdtsc();
    for (int i = 0; i < N; ++i) {
        auto q = p;    // atomic fetch_add(1) — LOCK XADD on x86
        sink(q.get());
    }
    uint64_t elapsed = rdtsc() - start;
    std::cout << "[shared_ptr copy]  cycles/iter: " << elapsed / N
              << "  (includes atomic lock xadd)\n";
}

// ─── 4. make_shared vs shared_ptr(new T) — allocation count ──────────────────

static int alloc_count = 0;

void* operator new(size_t n) {
    ++alloc_count;
    return std::malloc(n);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

void bench_make_shared_vs_new() {
    std::cout << "\n--- make_shared vs shared_ptr(new T) ---\n";

    alloc_count = 0;
    {
        auto p = std::make_shared<Order>(Order{100, 10, 1});
        sink(p.get());
    }
    std::cout << "make_shared<Order>:     " << alloc_count << " heap allocation(s)\n";

    alloc_count = 0;
    {
        auto p = std::shared_ptr<Order>(new Order{100, 10, 1});
        sink(p.get());
    }
    std::cout << "shared_ptr(new Order):  " << alloc_count << " heap allocation(s)\n";
    std::cout << "(make_shared fuses object + control block = 1 alloc vs 2)\n";
}

// ─── 5. shared_ptr cycle and weak_ptr fix ────────────────────────────────────

struct Node {
    int value;
    std::shared_ptr<Node> next;   // cycle if two nodes point to each other
    // Fix: std::weak_ptr<Node> next;
    explicit Node(int v) : value{v} {}
    ~Node() { std::cout << "~Node(" << value << ")\n"; }
};

void demo_cycle_leak() {
    std::cout << "\n--- shared_ptr cycle (memory leak) ---\n";
    {
        auto a = std::make_shared<Node>(1);
        auto b = std::make_shared<Node>(2);
        a->next = b;
        b->next = a;   // ← CYCLE: a→b→a
        // use_count(a) == 2, use_count(b) == 2
        // When scope exits: use_count goes 2→1, never reaches 0 → LEAK
        std::cout << "Scope exiting — destructors should run here...\n";
    }
    std::cout << "  ← Neither Node was destroyed! (memory leak)\n";
}

struct NodeSafe {
    int value;
    std::weak_ptr<NodeSafe> next;   // breaks the cycle
    explicit NodeSafe(int v) : value{v} {}
    ~NodeSafe() { std::cout << "~NodeSafe(" << value << ")\n"; }
};

void demo_cycle_fixed() {
    std::cout << "\n--- weak_ptr breaks the cycle ---\n";
    {
        auto a = std::make_shared<NodeSafe>(1);
        auto b = std::make_shared<NodeSafe>(2);
        a->next = b;   // weak reference — doesn't increase use_count
        b->next = a;
        std::cout << "Scope exiting...\n";
    }
    std::cout << "  ← Both nodes destroyed correctly.\n";
}

// ─── 6. Custom deleter ────────────────────────────────────────────────────────

void demo_custom_deleter() {
    std::cout << "\n--- Custom deleter ---\n";

    // RAII file handle using unique_ptr
    auto file = std::unique_ptr<FILE, decltype(&fclose)>(
        fopen("/dev/null", "w"), &fclose
    );
    if (file) {
        std::cout << "File opened, unique_ptr will fclose() on destruction.\n";
    }
    // file goes out of scope → fclose called automatically

    // Aligned memory with custom deleter
    auto aligned_buf = std::unique_ptr<char, decltype(&std::free)>(
        static_cast<char*>(std::aligned_alloc(64, 4096)),
        &std::free
    );
    std::cout << "64-byte-aligned buffer at: " << static_cast<void*>(aligned_buf.get())
              << " (addr % 64 == " << (reinterpret_cast<uintptr_t>(aligned_buf.get()) % 64) << ")\n";
}

// ─── 7. HFT summary ───────────────────────────────────────────────────────────

void hft_rules() {
    std::cout << "\n=== HFT Smart Pointer Rules ===\n"
              << "Hot path:     by-value > unique_ptr > raw (non-owning) >> shared_ptr\n"
              << "shared_ptr:   OK for setup/teardown, NEVER in the tick handler\n"
              << "unique_ptr:   zero overhead, move-only, perfect for heap objects\n"
              << "weak_ptr:     breaks cycles, use in caches/observers off hot path\n"
              << "make_shared:  always prefer — one allocation, better cache locality\n";
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Smart Pointers ===\n\n";
    show_sizeof();
    bench_unique_ptr_move();
    bench_shared_ptr_copy();
    bench_make_shared_vs_new();
    demo_cycle_leak();
    demo_cycle_fixed();
    demo_custom_deleter();
    hft_rules();
    return 0;
}
