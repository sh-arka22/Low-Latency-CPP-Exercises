// Chapter 7 — Memory Management
// Topic 6: Memory Pools and Arena Allocators
// Compile: g++ -std=c++20 -O2 -o 06 06_memory_pool_arena.cpp
// Run:     ./06

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <vector>

static inline uint64_t rdtsc() {
#if defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return static_cast<uint64_t>(hi) << 32 | lo;
#else
    return 0;
#endif
}

template <typename T>
void sink(T const& v) { asm volatile("" : : "r,m"(v) : "memory"); }

// ─── Order struct (our allocation target) ────────────────────────────────────

struct Order {
    int64_t price{0};
    int32_t qty{0};
    int32_t id{0};
    bool trivial() const { return true; }
};

static_assert(std::is_trivially_destructible_v<Order>);

static constexpr int BENCH_N = 500'000;

// ─── 1. BumpPointerArena ─────────────────────────────────────────────────────
// O(1) allocation, no individual deallocation.
// Reset entire arena at once.

class BumpPointerArena {
    char*  buf_;
    char*  cur_;
    char*  end_;
    bool   owns_;   // did we mmap/malloc the buffer?

public:
    // External buffer (stack, mmap'd region, etc.)
    BumpPointerArena(void* buf, std::size_t size) noexcept
        : buf_{static_cast<char*>(buf)}
        , cur_{static_cast<char*>(buf)}
        , end_{static_cast<char*>(buf) + size}
        , owns_{false}
    {}

    // Heap-backed (for demo purposes — defeats the purpose in HFT)
    explicit BumpPointerArena(std::size_t size)
        : buf_{static_cast<char*>(std::aligned_alloc(64, size))}
        , cur_{buf_}
        , end_{buf_ + size}
        , owns_{true}
    { if (!buf_) throw std::bad_alloc{}; }

    ~BumpPointerArena() { if (owns_) std::free(buf_); }

    void* allocate(std::size_t bytes, std::size_t alignment = 8) {
        // Align current pointer
        uintptr_t cur = reinterpret_cast<uintptr_t>(cur_);
        uintptr_t aligned = (cur + alignment - 1) & ~(alignment - 1);
        char* result = reinterpret_cast<char*>(aligned);
        if (result + bytes > end_) throw std::bad_alloc{};
        cur_ = result + bytes;
        return result;
    }

    template <typename T, typename... Args>
    T* emplace(Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T));
        return new (mem) T(std::forward<Args>(args)...);
    }

    void reset() noexcept { cur_ = buf_; }

    std::size_t used()  const noexcept { return static_cast<std::size_t>(cur_ - buf_); }
    std::size_t capacity() const noexcept { return static_cast<std::size_t>(end_ - buf_); }
};

void demo_bump_arena() {
    std::cout << "--- BumpPointerArena ---\n";
    alignas(64) char buf[1 << 20];  // 1 MB on stack
    BumpPointerArena arena{buf, sizeof(buf)};

    // Allocate 1000 Orders
    for (int i = 0; i < 1000; ++i) {
        Order* o = arena.emplace<Order>(i * 100LL, i, i);
        sink(o->price);
    }
    std::cout << "  Allocated 1000 Orders. used=" << arena.used() << " bytes\n";

    // Reset — O(1) free all
    arena.reset();
    std::cout << "  After reset: used=" << arena.used() << " bytes\n\n";
}

// ─── 2. FixedPoolAllocator<T, N> ─────────────────────────────────────────────
// Fixed-size free list. O(1) alloc and dealloc.
// No heap after construction.

template <typename T, std::size_t PoolSize>
class FixedPool {
    union Slot {
        alignas(T) char  storage[sizeof(T)];
        Slot*            next;
    };

    std::array<Slot, PoolSize> pool_{};
    Slot* free_head_{nullptr};
    std::size_t allocated_{0};

public:
    FixedPool() noexcept {
        for (std::size_t i = 0; i + 1 < PoolSize; ++i)
            pool_[i].next = &pool_[i + 1];
        pool_[PoolSize - 1].next = nullptr;
        free_head_ = pool_.data();
    }

    template <typename... Args>
    T* allocate(Args&&... args) {
        if (!free_head_) throw std::bad_alloc{};
        Slot* slot = free_head_;
        free_head_ = free_head_->next;
        ++allocated_;
        return new (slot->storage) T(std::forward<Args>(args)...);
    }

    void deallocate(T* p) noexcept {
        if constexpr (!std::is_trivially_destructible_v<T>)
            p->~T();
        auto* slot = reinterpret_cast<Slot*>(p);
        slot->next = free_head_;
        free_head_ = slot;
        --allocated_;
    }

    std::size_t available() const noexcept {
        std::size_t n = 0;
        for (Slot* s = free_head_; s; s = s->next) ++n;
        return n;
    }

    std::size_t allocated() const noexcept { return allocated_; }
};

void demo_fixed_pool() {
    std::cout << "--- FixedPool<Order, 1024> ---\n";
    FixedPool<Order, 1024> pool;
    std::cout << "  Initial available: " << pool.available() << "\n";

    Order* o1 = pool.allocate(100LL, 50, 1);
    Order* o2 = pool.allocate(200LL, 30, 2);
    std::cout << "  After 2 allocs: allocated=" << pool.allocated()
              << ", available=" << pool.available() << "\n";

    pool.deallocate(o1);
    std::cout << "  After dealloc: allocated=" << pool.allocated()
              << ", available=" << pool.available() << "\n";

    // Show O(1) alloc+dealloc cycle
    pool.deallocate(o2);
    std::cout << "  Slots returned to pool. Zero heap calls after construction.\n\n";
}

// ─── 3. Arena with destructor tracking ───────────────────────────────────────
// For non-trivial types that need their destructors called.

class ManagedArena {
    std::vector<char> buf_;
    char* cur_;
    std::vector<std::function<void()>> dtors_;

public:
    explicit ManagedArena(std::size_t size)
        : buf_(size), cur_{buf_.data()}
    {}

    template <typename T, typename... Args>
    T* emplace(Args&&... args) {
        uintptr_t aligned = (reinterpret_cast<uintptr_t>(cur_) + alignof(T) - 1)
                             & ~(alignof(T) - 1);
        cur_ = reinterpret_cast<char*>(aligned) + sizeof(T);
        T* obj = new (reinterpret_cast<void*>(aligned)) T(std::forward<Args>(args)...);
        if constexpr (!std::is_trivially_destructible_v<T>)
            dtors_.push_back([obj]{ obj->~T(); });
        return obj;
    }

    ~ManagedArena() {
        for (auto it = dtors_.rbegin(); it != dtors_.rend(); ++it)
            (*it)();   // run in reverse construction order
    }
};

struct ComplexOrder {
    int id;
    std::vector<int> fills;  // non-trivial
    ComplexOrder(int i) : id{i}, fills{1, 2, 3} {}
    ~ComplexOrder() {
        std::cout << "    ~ComplexOrder(" << id << ")\n";
    }
};

void demo_managed_arena() {
    std::cout << "--- ManagedArena (non-trivial types) ---\n";
    {
        ManagedArena arena{4096};
        auto* a = arena.emplace<ComplexOrder>(1);
        auto* b = arena.emplace<ComplexOrder>(2);
        std::cout << "  Created ComplexOrder " << a->id << " and " << b->id << "\n";
        std::cout << "  Arena going out of scope — dtors run in reverse:\n";
    }  // dtors run here: b first, then a
    std::cout << "\n";
}

// ─── 4. Benchmark: new/delete vs arena ───────────────────────────────────────

void bench_allocators() {
    std::cout << "--- Benchmark: new/delete vs BumpArena (" << BENCH_N << " iters) ---\n";

    // new/delete
    {
        uint64_t t0 = rdtsc();
        for (int i = 0; i < BENCH_N; ++i) {
            auto* p = new Order{i * 100LL, i, i};
            sink(p->price);
            delete p;
        }
        std::cout << "  new/delete:  " << (rdtsc() - t0) / BENCH_N << " cycles/iter\n";
    }

    // Bump arena (stack buffer)
    {
        alignas(64) char buf[BENCH_N * sizeof(Order) * 2];
        BumpPointerArena arena{buf, sizeof(buf)};
        uint64_t t0 = rdtsc();
        for (int i = 0; i < BENCH_N; ++i) {
            auto* p = arena.emplace<Order>(i * 100LL, i, i);
            sink(p->price);
            // no dealloc — arena holds everything
        }
        uint64_t cycles = rdtsc() - t0;
        arena.reset();
        std::cout << "  BumpArena:   " << cycles / BENCH_N << " cycles/iter\n";
    }

    // PMR monotonic (for comparison)
    {
        alignas(64) char buf[BENCH_N * sizeof(Order) * 2];
        std::pmr::monotonic_buffer_resource res{buf, sizeof(buf)};
        uint64_t t0 = rdtsc();
        for (int i = 0; i < BENCH_N; ++i) {
            void* m = res.allocate(sizeof(Order), alignof(Order));
            auto* p = new (m) Order{i * 100LL, i, i};
            sink(p->price);
        }
        std::cout << "  PMR monotonic: " << (rdtsc() - t0) / BENCH_N << " cycles/iter\n";
    }

    std::cout << "\n";
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Memory Pools and Arenas ===\n\n";
    demo_bump_arena();
    demo_fixed_pool();
    demo_managed_arena();
    bench_allocators();

    std::cout << "=== Rules ===\n"
              << "  1. BumpArena: O(1) alloc, no individual dealloc, reset at once.\n"
              << "  2. FixedPool: O(1) alloc AND dealloc, fixed-size objects only.\n"
              << "  3. ManagedArena: handles non-trivial dtors, slightly more overhead.\n"
              << "  4. Goal: zero malloc() calls after startup on hot path.\n"
              << "  5. Use per-thread arenas reset at each tick for zero lock contention.\n";
    return 0;
}
