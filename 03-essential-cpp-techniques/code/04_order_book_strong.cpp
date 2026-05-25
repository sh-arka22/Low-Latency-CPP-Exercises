/**
 * Chapter 3 — Essential C++ Techniques
 * Topic 5 / Level 3 — HFT Challenge: OrderBook with strong exception
 *                     guarantee + ScopedTimer with circular log buffer.
 *
 * ── PROBLEM ──────────────────────────────────────────────────────────────────
 *  A real order book mutates two sides (bids/asks) when an order arrives.
 *  If the bid update succeeds but the ask update throws, the book is in
 *  an inconsistent state — partial visibility into the new order.
 *
 *  Strong exception guarantee via copy-and-swap:
 *    1) Make local copies of bids/asks (may throw — *this untouched).
 *    2) Apply the mutation to the copies (may throw — still untouched).
 *    3) std::swap the copies into *this (noexcept commit).
 *
 *  Either add_order succeeds completely, or the book is bit-for-bit
 *  identical to its pre-call state. No partial visibility, ever.
 *
 * ── ScopedTimer ──────────────────────────────────────────────────────────────
 *  RAII timer: start in ctor, on dtor compute elapsed ns and push into a
 *  shared circular buffer of samples. Cheap enough to wrap around hot paths.
 *  Circular buffer avoids unbounded growth and avoids allocating in the
 *  measured path (capacity fixed at compile time).
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>   // std::exit
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

// ─── Side =====================================================================
enum class Side { Bid, Ask };

struct Order {
    std::uint64_t id{};
    Side          side{};
    double        price{};
    std::uint32_t qty{};
};

// ─── CircularLog (fixed-capacity, lock-free for single producer) ─────────────
// Used by ScopedTimer to record latency samples without allocating.
template <std::size_t Capacity>
class CircularLog {
public:
    void push(std::uint64_t sample_ns) noexcept {
        const auto idx = head_.fetch_add(1, std::memory_order_relaxed) % Capacity;
        buf_[idx] = sample_ns;
    }

    // Snapshot of stored samples in insertion order (newest first up to N).
    // Not noexcept — copies into a vector for inspection.
    std::vector<std::uint64_t> snapshot() const {
        const auto h = head_.load(std::memory_order_relaxed);
        const auto n = std::min<std::size_t>(h, Capacity);
        std::vector<std::uint64_t> out;
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            // walk newest → oldest
            const auto idx = (h - 1 - i) % Capacity;
            out.push_back(buf_[idx]);
        }
        return out;
    }

    std::size_t size() const noexcept {
        return std::min<std::size_t>(head_.load(std::memory_order_relaxed), Capacity);
    }

private:
    std::array<std::uint64_t, Capacity> buf_{};
    std::atomic<std::size_t>            head_{0};
};

// One process-wide log for the demo. In real code, inject a reference.
inline CircularLog<128>& timing_log() {
    static CircularLog<128> log;
    return log;
}

// ─── ScopedTimer ─────────────────────────────────────────────────────────────
// On destruction, pushes (now - start) nanoseconds into the timing log.
// Move-only and noexcept everywhere so it never throws from the hot path.
class ScopedTimer {
public:
    ScopedTimer() noexcept
        : start_{std::chrono::steady_clock::now()}, active_{true} {}

    ~ScopedTimer() {
        if (!active_) return;
        const auto end = std::chrono::steady_clock::now();
        const auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             end - start_).count();
        timing_log().push(static_cast<std::uint64_t>(ns));
    }

    ScopedTimer(const ScopedTimer&)            = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    ScopedTimer(ScopedTimer&& other) noexcept
        : start_{other.start_}, active_{other.active_} {
        other.active_ = false;
    }
    ScopedTimer& operator=(ScopedTimer&&) = delete;  // not needed; keep simple

private:
    std::chrono::steady_clock::time_point start_;
    bool                                  active_;
};

// ─── OrderBook ───────────────────────────────────────────────────────────────
// Two sides held as price-keyed maps. add_order updates both sides
// atomically (from the caller's perspective): either both reflect the
// new order, or neither does.
class OrderBook {
public:
    using PriceLevels = std::map<double, std::uint32_t>;  // price → total qty

    void add_order(const Order& o) {
        // ── Step 1: copy the current state (may throw). *this untouched.
        auto bids_tmp = bids_;
        auto asks_tmp = asks_;

        // ── Step 2: mutate the copies (may throw). *this still untouched.
        if (o.side == Side::Bid) bids_tmp[o.price] += o.qty;
        else                     asks_tmp[o.price] += o.qty;

        // ── Step 3: commit. swap is noexcept on std::map.
        bids_.swap(bids_tmp);
        asks_.swap(asks_tmp);
    }

    // Test hook: simulate a throw partway through the mutation step.
    // Used by the failure test to prove the strong guarantee.
    void add_order_with_injected_throw(const Order& o) {
        auto bids_tmp = bids_;
        auto asks_tmp = asks_;

        if (o.side == Side::Bid) {
            bids_tmp[o.price] += o.qty;
            throw std::runtime_error("injected after bid update, before swap");
        } else {
            asks_tmp[o.price] += o.qty;
            throw std::runtime_error("injected after ask update, before swap");
        }
        bids_.swap(bids_tmp);   // never reached
        asks_.swap(asks_tmp);
    }

    const PriceLevels& bids() const noexcept { return bids_; }
    const PriceLevels& asks() const noexcept { return asks_; }

private:
    PriceLevels bids_;
    PriceLevels asks_;
};

// ─── Demo / verification ─────────────────────────────────────────────────────

static void print_book(const OrderBook& b, const char* label) {
    std::cout << label << "  bids={";
    for (const auto& [p, q] : b.bids()) std::cout << " " << p << ":" << q;
    std::cout << " }  asks={";
    for (const auto& [p, q] : b.asks()) std::cout << " " << p << ":" << q;
    std::cout << " }\n";
}

static void section_orderbook_normal() {
    std::cout << "\n=== OrderBook: normal path ===\n";
    OrderBook book;
    {
        ScopedTimer t;
        book.add_order({1, Side::Bid, 100.0,  5});
        book.add_order({2, Side::Bid, 100.0,  3});
        book.add_order({3, Side::Ask, 101.0, 10});
        book.add_order({4, Side::Ask, 102.0,  2});
    }   // ← ScopedTimer pushes elapsed-ns into the log here
    print_book(book, "after 4 orders:");
}

static void section_orderbook_strong_guarantee() {
    std::cout << "\n=== OrderBook: strong guarantee under throw ===\n";
    OrderBook book;
    book.add_order({10, Side::Bid, 99.5, 7});
    book.add_order({11, Side::Ask, 100.5, 4});

    // Snapshot what we expect to survive the throw.
    const auto bids_before = book.bids();
    const auto asks_before = book.asks();

    try {
        ScopedTimer t;
        book.add_order_with_injected_throw({12, Side::Bid, 99.5, 100});
    } catch (const std::exception& e) {
        std::cout << "caught: " << e.what() << "\n";
    }

    const bool bids_unchanged = (book.bids() == bids_before);
    const bool asks_unchanged = (book.asks() == asks_before);
    std::cout << "bids unchanged after throw? " << bids_unchanged << "\n";
    std::cout << "asks unchanged after throw? " << asks_unchanged << "\n";
    print_book(book, "post-throw state:");

    if (!bids_unchanged || !asks_unchanged) {
        std::cerr << "STRONG GUARANTEE VIOLATED\n";
        std::exit(1);
    }
}

static void section_timing_report() {
    std::cout << "\n=== Timing report (newest first) ===\n";
    const auto samples = timing_log().snapshot();
    std::cout << "recorded " << samples.size() << " samples:\n";
    for (std::size_t i = 0; i < samples.size(); ++i) {
        std::cout << "  [" << i << "] " << samples[i] << " ns\n";
    }
}

int main() {
    section_orderbook_normal();
    section_orderbook_strong_guarantee();
    section_timing_report();
    return 0;
}
