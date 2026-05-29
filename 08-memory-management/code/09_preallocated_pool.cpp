// Pre-Allocated Pool — Concise Demo
// Compile: g++ -std=c++20 -O2 -o 09 09_preallocated_pool.cpp
// Run:     ./09

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <type_traits>

// ─── Timing ──────────────────────────────────────────────────────────────────

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

// ─── PreAllocatedPool ────────────────────────────────────────────────────────
//
//  HOW IT WORKS:
//    Slots are a union: either a live T, or a next-pointer in the free list.
//
//    Construction:  free_head → [0] → [1] → ... → [N-1] → null
//    allocate():    pop head off free list, placement-new T      O(1)
//    deallocate():  destroy T, push slot back onto head          O(1)
//
//  WHY IT'S FAST:
//    Zero malloc/free after construction. Zero fragmentation.
//    Deterministic O(1) — no amortised spikes, no locks, no syscalls.

template <typename T, std::size_t Capacity>
class PreAllocatedPool {
    union Slot {
        alignas(T) unsigned char storage[sizeof(T)];
        Slot* next;
    };

    Slot        storage_[Capacity];
    Slot*       free_head_{nullptr};
    std::size_t in_use_{0};
    std::size_t hwm_{0};  // high-water mark

public:
    PreAllocatedPool() noexcept {
        for (std::size_t i = 0; i + 1 < Capacity; ++i)
            storage_[i].next = &storage_[i + 1];
        storage_[Capacity - 1].next = nullptr;
        free_head_ = &storage_[0];
    }

    // Non-copyable
    PreAllocatedPool(const PreAllocatedPool&)            = delete;
    PreAllocatedPool& operator=(const PreAllocatedPool&) = delete;

    template <typename... Args>
    [[nodiscard]] T* allocate(Args&&... args) {
        if (!free_head_) throw std::bad_alloc{};
        Slot* slot = free_head_;
        free_head_ = free_head_->next;
        ++in_use_;
        if (in_use_ > hwm_) hwm_ = in_use_;
        return new (slot->storage) T(std::forward<Args>(args)...);
    }

    void deallocate(T* p) noexcept {
        if constexpr (!std::is_trivially_destructible_v<T>) p->~T();
        auto* slot = reinterpret_cast<Slot*>(p);
        slot->next = free_head_;
        free_head_ = slot;
        --in_use_;
    }

    std::size_t capacity()        const noexcept { return Capacity; }
    std::size_t in_use()          const noexcept { return in_use_; }
    std::size_t available()       const noexcept { return Capacity - in_use_; }
    std::size_t high_water_mark() const noexcept { return hwm_; }
    bool        full()            const noexcept { return !free_head_; }
};

// ─── Demo Type ───────────────────────────────────────────────────────────────

struct Order {
    int64_t price;
    int32_t qty;
    int32_t id;
    char    symbol[8];
    Order(int64_t p, int32_t q, int32_t i, const char* s = "AAPL")
        : price{p}, qty{q}, id{i}, symbol{} {
        std::strncpy(symbol, s, 7);
    }
};
static_assert(std::is_trivially_destructible_v<Order>);

// ─── 1. Basic usage + free-list walkthrough ──────────────────────────────────

void demo() {
    std::cout << "=== Basic Usage ===\n\n";
    PreAllocatedPool<Order, 4> pool;

    std::cout << "Initial:     free_head → [0]→[1]→[2]→[3]→null  available=" << pool.available() << "\n";

    Order* a = pool.allocate(10050, 100, 1, "AAPL");
    std::cout << "alloc(AAPL): free_head → [1]→[2]→[3]→null      available=" << pool.available() << "\n";

    Order* b = pool.allocate(20075, 200, 2, "MSFT");
    std::cout << "alloc(MSFT): free_head → [2]→[3]→null           available=" << pool.available() << "\n";

    pool.deallocate(a);
    std::cout << "dealloc(a):  free_head → [0]→[2]→[3]→null       available=" << pool.available() << "\n";

    Order* c = pool.allocate(30025, 50, 3, "GOOG");
    std::cout << "alloc(GOOG): free_head → [2]→[3]→null (reused [0])  available=" << pool.available() << "\n";

    std::cout << "\nLive orders:\n";
    std::cout << "  b: " << b->symbol << " price=" << b->price << "\n";
    std::cout << "  c: " << c->symbol << " price=" << c->price << "\n";

    pool.deallocate(b);
    pool.deallocate(c);
    std::cout << "\nAll freed. high_water_mark=" << pool.high_water_mark() << "\n\n";
}

// ─── 2. Benchmark ────────────────────────────────────────────────────────────

static constexpr int N = 1'000'000;

void benchmark() {
    std::cout << "=== Benchmark (" << N << " alloc+dealloc) ===\n\n";

    // new/delete
    {
        uint64_t t0 = rdtsc();
        for (int i = 0; i < N; ++i) {
            auto* p = new Order{i * 100LL, i, i};
            sink(p->price);
            delete p;
        }
        std::cout << "  new/delete:        " << (rdtsc() - t0) / N << " cycles/iter\n";
    }

    // PreAllocatedPool
    {
        static PreAllocatedPool<Order, N> pool;
        uint64_t t0 = rdtsc();
        for (int i = 0; i < N; ++i) {
            Order* p = pool.allocate(i * 100LL, i, i);
            sink(p->price);
            pool.deallocate(p);
        }
        std::cout << "  PreAllocatedPool:  " << (rdtsc() - t0) / N << " cycles/iter\n";
    }

    std::cout << "\n";
}

// ─── 3. HFT pattern: per-tick usage ──────────────────────────────────────────

void demo_hft() {
    std::cout << "=== HFT Pattern ===\n\n";

    PreAllocatedPool<Order, 256> pool;  // pre-allocate at startup

    struct Tick { int64_t price; int32_t qty; int32_t id; const char* sym; };
    Tick ticks[] = {{10050,100,1,"AAPL"}, {30025,50,2,"GOOG"}, {20075,300,3,"MSFT"}};

    for (auto& t : ticks) {
        Order* o = pool.allocate(t.price, t.qty, t.id, t.sym);  // O(1), 0 malloc
        sink(o->price);                                          // process
        std::cout << "  tick: " << o->symbol << " price=" << o->price << "\n";
        pool.deallocate(o);                                      // O(1), 0 free
    }

    std::cout << "\n  malloc calls on hot path: 0\n";
    std::cout << "  high_water_mark: " << pool.high_water_mark() << "\n";
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    demo();
    benchmark();
    demo_hft();
    return 0;
}
