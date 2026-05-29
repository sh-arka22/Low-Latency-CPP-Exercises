// Chapter 7 — Memory Management
// Topic 3: Custom Allocators — PMR, stack allocator, pool allocator
// Compile: g++ -std=c++20 -O2 -o 03 03_custom_allocator.cpp
// Run:     ./03

#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory_resource>  // std::pmr — C++17
#include <new>
#include <stdexcept>
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

static constexpr int N = 100'000;

struct Order {
    int64_t price{0};
    int32_t qty{0};
    int32_t id{0};
};

// ─── 1. PMR monotonic_buffer_resource (bump pointer) ─────────────────────────

void demo_monotonic_buffer() {
    std::cout << "--- monotonic_buffer_resource (bump pointer) ---\n";

    // 1 MB stack buffer — no heap!
    alignas(64) char buf[1 << 20];
    std::pmr::monotonic_buffer_resource arena{buf, sizeof(buf)};

    // Attach pmr containers to the arena
    std::pmr::vector<Order> orders{&arena};
    orders.reserve(N);   // one bump-pointer advance

    for (int i = 0; i < N; ++i)
        orders.push_back(Order{i * 100LL, i, i});

    std::cout << "  Allocated " << N << " Orders via bump pointer.\n";
    std::cout << "  Individual dealloc: NOT possible (no-op).\n";
    std::cout << "  arena.release(): resets in O(1) — entire arena freed.\n";
    arena.release();
    std::cout << "  Arena released.\n\n";
}

// ─── 2. PMR pool resource (free-list per size class) ─────────────────────────

void demo_pool_resource() {
    std::cout << "--- unsynchronized_pool_resource (free list) ---\n";

    alignas(64) char buf[1 << 20];
    std::pmr::monotonic_buffer_resource upstream{buf, sizeof(buf)};
    std::pmr::unsynchronized_pool_resource pool{&upstream};

    std::pmr::vector<Order> orders{&pool};
    for (int i = 0; i < 1000; ++i)
        orders.push_back(Order{i * 100LL, i, i});

    std::cout << "  1000 Orders allocated from pool resource.\n";
    std::cout << "  Individual deallocations return slots to the pool free list.\n\n";
}

// ─── 3. Hand-rolled StackAllocator<T,N> ──────────────────────────────────────
// Classic pre-C++17 allocator. Works for std::vector, std::list, etc.

template <typename T, std::size_t BufSize>
class StackAllocator {
public:
    using value_type = T;

    // Required for std::vector to rebind the allocator internally
    template <typename U>
    struct rebind { using other = StackAllocator<U, BufSize>; };

    StackAllocator() noexcept = default;

    // Rebind copy constructor (required by std::vector internals)
    template <typename U>
    StackAllocator(const StackAllocator<U, BufSize>&) noexcept {}

    T* allocate(std::size_t n) {
        std::size_t required = n * sizeof(T);
        std::size_t aligned_cur = (cur_ + alignof(T) - 1) & ~(alignof(T) - 1);
        if (aligned_cur + required > BufSize)
            throw std::bad_alloc{};
        cur_ = aligned_cur + required;
        return reinterpret_cast<T*>(buf_ + aligned_cur);
    }

    void deallocate(T* /*p*/, std::size_t /*n*/) noexcept {
        // stack allocator: no individual deallocation — all freed when object destroyed
    }

private:
    alignas(std::max_align_t) char buf_[BufSize]{};
    std::size_t cur_{0};
};

template <typename T, typename U, std::size_t N>
bool operator==(const StackAllocator<T,N>&, const StackAllocator<U,N>&) noexcept { return true; }
template <typename T, typename U, std::size_t N>
bool operator!=(const StackAllocator<T,N>&, const StackAllocator<U,N>&) noexcept { return false; }

void demo_stack_allocator() {
    std::cout << "--- Hand-rolled StackAllocator ---\n";
    constexpr std::size_t BUF = 1024 * sizeof(Order);
    StackAllocator<Order, BUF> alloc;

    std::vector<Order, StackAllocator<Order, BUF>> orders{alloc};
    orders.reserve(100);

    for (int i = 0; i < 100; ++i)
        orders.push_back(Order{i * 100LL, i, i});

    std::cout << "  100 Orders in StackAllocator-backed vector (zero heap).\n";
    std::cout << "  sizeof(alloc) = " << sizeof(alloc) << " bytes (contains the buffer itself).\n\n";
}

// ─── 4. PoolAllocator<T> — fixed-size free list ──────────────────────────────

template <typename T, std::size_t PoolSize>
class PoolAllocator {
public:
    using value_type = T;

    PoolAllocator() noexcept {
        // Link all slots into the free list
        auto* s = slots();
        for (std::size_t i = 0; i + 1 < PoolSize; ++i)
            s[i].next = &s[i + 1];
        s[PoolSize - 1].next = nullptr;
        free_head_ = s;
    }

    template <typename U>
    PoolAllocator(const PoolAllocator<U, PoolSize>&) noexcept {}

    T* allocate(std::size_t n) {
        if (n != 1 || !free_head_) throw std::bad_alloc{};
        Slot* slot = free_head_;
        free_head_ = free_head_->next;
        return reinterpret_cast<T*>(slot);
    }

    void deallocate(T* p, std::size_t) noexcept {
        auto* slot = reinterpret_cast<Slot*>(p);
        slot->next = free_head_;
        free_head_ = slot;
    }

private:
    union Slot {
        alignas(T) char  storage[sizeof(T)];
        Slot*            next;
    };

    Slot* slots() { return reinterpret_cast<Slot*>(storage_); }

    alignas(Slot) char  storage_[sizeof(Slot) * PoolSize]{};
    Slot* free_head_{nullptr};
};

template <typename T, typename U, std::size_t N>
bool operator==(const PoolAllocator<T,N>&, const PoolAllocator<U,N>&) noexcept { return true; }
template <typename T, typename U, std::size_t N>
bool operator!=(const PoolAllocator<T,N>&, const PoolAllocator<U,N>&) noexcept { return false; }

// ─── 5. Benchmark: default vs PMR vs PoolAllocator ───────────────────────────

void bench_allocators() {
    std::cout << "--- Allocation Benchmark (" << N << " Orders) ---\n";

    // Default (glibc malloc):
    {
        uint64_t t0 = rdtsc();
        for (int i = 0; i < N; ++i) {
            auto* p = new Order{i * 100LL, i, i};
            sink(p->price);
            delete p;
        }
        std::cout << "[default new/delete] cycles/iter: " << (rdtsc() - t0) / N << "\n";
    }

    // PMR monotonic (bump pointer):
    {
        alignas(64) char buf[N * sizeof(Order) * 2];
        std::pmr::monotonic_buffer_resource arena{buf, sizeof(buf)};
        uint64_t t0 = rdtsc();
        for (int i = 0; i < N; ++i) {
            auto* p = static_cast<Order*>(arena.allocate(sizeof(Order), alignof(Order)));
            new (p) Order{i * 100LL, i, i};
            sink(p->price);
            p->~Order();
            // no individual free
        }
        std::cout << "[PMR bump pointer]   cycles/iter: " << (rdtsc() - t0) / N << "\n";
        arena.release();
    }
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Custom Allocators ===\n\n";
    demo_monotonic_buffer();
    demo_pool_resource();
    demo_stack_allocator();
    bench_allocators();

    std::cout << "\n=== Key Takeaways ===\n"
              << "  monotonic_buffer_resource: ~1-3 ns/alloc, no dealloc possible\n"
              << "  pool resource:             ~5-10 ns/alloc, O(1) dealloc\n"
              << "  default new/delete:        ~50-500 ns/alloc, non-deterministic\n"
              << "  HFT pattern: allocate from bump-pointer arena per tick, reset at tick end\n";
    return 0;
}
