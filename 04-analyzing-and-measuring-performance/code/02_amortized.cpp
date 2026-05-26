/**
 * Chapter 3 — Measuring Performance
 * Topic 2: Amortized Complexity
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra 02_amortized.cpp -o amortized
 *
 * Key ideas:
 *   - Amortized O(1) ≠ average O(1). Amortized is a guarantee over a sequence.
 *   - vector::push_back: O(n) on resize, O(1) amortized over n total pushes.
 *   - HFT rule: reserve() upfront to eliminate all reallocation on the hot path.
 */

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <deque>
#include <iostream>
#include <vector>

// ─── helpers ────────────────────────────────────────────────────────────────

using Clock = std::chrono::steady_clock;

template <typename F>
long long time_ns(F&& f) {
    auto t0 = Clock::now();
    f();
    auto t1 = Clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
}

// ─── SECTION 1: Tracking vector growth ──────────────────────────────────────
// Watch capacity jump — each jump = reallocation = O(n) move of all elements

void section_vector_growth() {
    std::cout << "\n=== SECTION 1: vector capacity doubling ===\n";
    std::vector<int> v;
    size_t prev_capacity = 0;
    int reallocs = 0;

    for (int i = 0; i < 1000; ++i) {
        v.push_back(i);
        if (v.capacity() != prev_capacity) {
            std::cout << "size=" << v.size()
                      << "  capacity jumped to " << v.capacity()
                      << "  (realloc #" << ++reallocs << ")\n";
            prev_capacity = v.capacity();
        }
    }
    // KEY INSIGHT: capacity roughly doubles each time → for n total pushes,
    // total realloc work = n/2 + n/4 + n/8 + ... < n → amortized O(1) per push.
}

// ─── SECTION 2: reserve() eliminates reallocations ──────────────────────────

void section_reserve() {
    std::cout << "\n=== SECTION 2: reserve() — zero reallocations ===\n";

    constexpr int N = 1'000;

    // Without reserve: count reallocations
    {
        std::vector<int> v;
        int reallocs = 0;
        size_t prev = 0;
        for (int i = 0; i < N; ++i) {
            v.push_back(i);
            if (v.capacity() != prev) { ++reallocs; prev = v.capacity(); }
        }
        std::cout << "Without reserve: " << reallocs << " reallocations for "
                  << N << " push_backs\n";
    }

    // With reserve: should be 0 reallocations
    {
        std::vector<int> v;
        v.reserve(N);           // <-- allocate once upfront
        int reallocs = 0;
        size_t prev = v.capacity();
        for (int i = 0; i < N; ++i) {
            v.push_back(i);
            if (v.capacity() != prev) { ++reallocs; prev = v.capacity(); }
        }
        std::cout << "With reserve:    " << reallocs << " reallocations for "
                  << N << " push_backs\n";
        assert(reallocs == 0 && "reserve() should prevent all reallocations");
    }
}

// ─── SECTION 3: emplace_back vs push_back ───────────────────────────────────
// emplace_back constructs in-place; push_back copies/moves an existing object

struct Expensive {
    int id;
    explicit Expensive(int id_) : id(id_) {
        // std::cout << "Constructed " << id << "\n";
    }
    Expensive(const Expensive&) {
        // std::cout << "Copied!\n";
    }
    Expensive(Expensive&&) noexcept {
        // std::cout << "Moved!\n";
    }
};

void section_emplace_back() {
    std::cout << "\n=== SECTION 3: emplace_back vs push_back ===\n";
    std::vector<Expensive> v;
    v.reserve(4);

    // push_back: constructs temporary, then moves into vector
    v.push_back(Expensive{1});   // construct Expensive{1}, move into v

    // emplace_back: constructs directly in the vector's storage
    v.emplace_back(2);           // forwards int 2 to Expensive(int) in-place

    std::cout << "v has " << v.size() << " elements (IDs: "
              << v[0].id << ", " << v[1].id << ")\n";
    // No unnecessary copies if move ctor is noexcept (verified by is_nothrow_move_constructible)
    static_assert(std::is_nothrow_move_constructible_v<Expensive>,
                  "Move must be noexcept for vector to use moves on realloc");
}

// ─── SECTION 4: Amortized O(1) — push_back timing ───────────────────────────

void section_push_back_timing() {
    std::cout << "\n=== SECTION 4: push_back amortized timing ===\n";
    constexpr int N = 1'000'000;

    // Without reserve — may reallocate
    auto ns_no_reserve = time_ns([&]{
        std::vector<int> v;
        for (int i = 0; i < N; ++i) v.push_back(i);
    });

    // With reserve — no reallocations
    auto ns_with_reserve = time_ns([&]{
        std::vector<int> v;
        v.reserve(N);
        for (int i = 0; i < N; ++i) v.push_back(i);
    });

    std::cout << "1M push_backs without reserve: " << ns_no_reserve   / 1'000'000 << " ms\n";
    std::cout << "1M push_backs with reserve:    " << ns_with_reserve / 1'000'000 << " ms\n";
    std::cout << "Per-push overhead of reallocs: "
              << (ns_no_reserve - ns_with_reserve) / N << " ns/push (approx)\n";
}

// ─── SECTION 4.5: GrowthTracker ─────────────────────────────────────────────
// Wrap a vector to log capacity each time it changes

template <typename T>
class GrowthTracker {
    std::vector<T> vec;
    size_t prev_capacity = 0;

public:
    void push_back(const T& val) {
        vec.push_back(val);
        check_capacity();
    }
    
    void push_back(T&& val) {
        vec.push_back(std::move(val));
        check_capacity();
    }
    
    template <typename... Args>
    void emplace_back(Args&&... args) {
        vec.emplace_back(std::forward<Args>(args)...);
        check_capacity();
    }
    
    size_t size() const { return vec.size(); }
    size_t capacity() const { return vec.capacity(); }

private:
    void check_capacity() {
        if (vec.capacity() != prev_capacity) {
            std::cout << "[GrowthTracker] Capacity grew from " << prev_capacity 
                      << " to " << vec.capacity() << " (size is " << vec.size() << ")\n";
            prev_capacity = vec.capacity();
        }
    }
};

void section_growth_tracker() {
    std::cout << "\n=== SECTION 4.5: GrowthTracker Wrapper ===\n";
    GrowthTracker<int> tracker;
    for (int i = 0; i < 100; ++i) {
        tracker.push_back(i);
    }
}

// ─── SECTION 5: PreallocatedQueue stub ──────────────────────────────────────
// HFT LEVEL 3 TASK — fill this in yourself

template <typename T, size_t N>
class PreallocatedQueue {
    // TODO: implement using std::array<T, N> as ring buffer
    // head_, tail_ as size_t indices
    // push() → O(1) worst-case guaranteed (no allocation)
    // pop()  → O(1) worst-case guaranteed
    // full() / empty() predicates

    std::array<T, N> buf_{};
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t size_ = 0;

public:
    bool push(const T& val) {
        if (size_ == N) return false;   // full
        buf_[tail_] = val;
        tail_ = (tail_ + 1) % N;
        ++size_;
        return true;
    }

    bool pop(T& out) {
        if (size_ == 0) return false;   // empty
        out = buf_[head_];
        head_ = (head_ + 1) % N;
        --size_;
        return true;
    }

    bool empty() const { return size_ == 0; }
    bool full()  const { return size_ == N; }
    size_t size() const { return size_; }
};

void section_preallocated_queue() {
    std::cout << "\n=== SECTION 5: PreallocatedQueue<int,8> ===\n";

    PreallocatedQueue<int, 8> q;
    assert(q.empty());

    for (int i = 0; i < 8; ++i) assert(q.push(i));
    assert(q.full());
    assert(!q.push(99));   // should fail — queue is full

    int val;
    for (int expected = 0; expected < 8; ++expected) {
        assert(q.pop(val));
        assert(val == expected);
    }
    assert(q.empty());

    std::cout << "PreallocatedQueue<int,8>: all assertions passed\n";

    // Benchmark vs std::deque
    constexpr int M = 100'000;
    constexpr size_t CAP = 1024;

    auto ns_pa = time_ns([&]{
        PreallocatedQueue<int, CAP> pq;
        for (int i = 0; i < M; ++i) {
            pq.push(i % CAP);
            int dummy;
            if (!pq.empty()) pq.pop(dummy);
        }
    });

    auto ns_deque = time_ns([&]{
        std::deque<int> dq;
        for (int i = 0; i < M; ++i) {
            dq.push_back(i);
            if (!dq.empty()) dq.pop_front();
        }
    });

    std::cout << M << " push+pop: PreallocatedQueue=" << ns_pa   << " ns"
              << "  std::deque=" << ns_deque << " ns\n";
    std::cout << "Ratio: " << (double)ns_deque / ns_pa << "× faster\n";
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    section_vector_growth();
    section_reserve();
    section_emplace_back();
    section_push_back_timing();
    section_growth_tracker();
    section_preallocated_queue();

    std::cout << "\n✓ All amortized sections complete.\n";
    return 0;
}
