// Chapter 7 — Memory Management
// Topic 5: Placement New, Manual Lifecycle, std::launder
// Compile: g++ -std=c++20 -O2 -o 05 05_placement_new.cpp
// Run:     ./05

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <new>         // std::launder, placement new
#include <type_traits>

// ─── 1. Placement new basics ─────────────────────────────────────────────────

struct Widget {
    int id;
    double value;

    Widget(int i, double v) : id{i}, value{v} {
        std::cout << "  Widget(" << id << ", " << value << ") constructed\n";
    }
    ~Widget() {
        std::cout << "  ~Widget(" << id << ")\n";
    }
};

void demo_placement_new_basics() {
    std::cout << "--- Placement New Basics ---\n";

    // Allocate raw storage (no constructor call)
    alignas(Widget) char buf[sizeof(Widget)];
    std::cout << "  Storage allocated on stack at: " << static_cast<void*>(buf) << "\n";

    // Construct Widget in existing storage:
    Widget* p = new (buf) Widget{42, 3.14};
    std::cout << "  id=" << p->id << ", value=" << p->value << "\n";

    // Manually call destructor (does NOT free memory):
    p->~Widget();

    // Reuse the same storage for a new Widget:
    Widget* q = new (buf) Widget{99, 2.71};
    std::cout << "  Reused storage: id=" << q->id << ", value=" << q->value << "\n";
    q->~Widget();

    std::cout << "  buf[] still on the stack — no heap involved.\n\n";
}

// ─── 2. std::launder — pointer provenance ────────────────────────────────────

void demo_launder() {
    std::cout << "--- std::launder ---\n";

    alignas(Widget) char buf[sizeof(Widget)];

    Widget* p1 = new (buf) Widget{1, 1.0};
    p1->~Widget();

    // Construct a NEW Widget in the same buffer:
    new (buf) Widget{2, 2.0};

    // WRONG (UB): compiler may have cached values from p1
    // Widget* p_wrong = reinterpret_cast<Widget*>(buf);  // do NOT use

    // CORRECT: launder tells the compiler "the object at this address has changed"
    Widget* p2 = std::launder(reinterpret_cast<Widget*>(buf));
    std::cout << "  After launder: id=" << p2->id << ", value=" << p2->value
              << " (should be 2, 2.0)\n";
    p2->~Widget();
    std::cout << "\n";
}

// ─── 3. is_trivially_copyable — when memcpy is legal ─────────────────────────

struct TrivialOrder {
    int64_t price;
    int32_t qty;
    int32_t id;
};
static_assert(std::is_trivially_copyable_v<TrivialOrder>,
              "TrivialOrder must be trivially copyable for safe memcpy");

struct NonTrivialOrder {
    int64_t price;
    int32_t qty;
    std::string symbol;   // std::string has a non-trivial copy constructor
};
static_assert(!std::is_trivially_copyable_v<NonTrivialOrder>,
              "NonTrivialOrder is NOT trivially copyable (has std::string)");

void demo_trivial_copy() {
    std::cout << "--- Trivially Copyable ---\n";
    std::cout << "TrivialOrder    trivially_copyable: "
              << std::is_trivially_copyable_v<TrivialOrder> << "\n";
    std::cout << "NonTrivialOrder trivially_copyable: "
              << std::is_trivially_copyable_v<NonTrivialOrder> << "\n";

    // Safe memcpy for trivial type:
    TrivialOrder src{1000LL, 50, 7};
    TrivialOrder dst;
    std::memcpy(&dst, &src, sizeof(TrivialOrder));
    std::cout << "After memcpy: price=" << dst.price << ", qty=" << dst.qty
              << ", id=" << dst.id << "\n\n";
}

// ─── 4. RingSlot — reusing storage without heap ──────────────────────────────

template <typename T, std::size_t N>
class PlacementRing {
    static_assert(std::is_trivially_destructible_v<T> ||
                  true,  // we'll handle dtors manually
                  "All types supported; trivially destructible avoids manual dtor");

    alignas(T) char storage_[sizeof(T) * N]{};
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t mask_{N - 1};

    T* slot(std::size_t i) {
        return std::launder(reinterpret_cast<T*>(storage_ + sizeof(T) * (i & mask_)));
    }

public:
    bool empty() const noexcept { return head_ == tail_; }
    bool full()  const noexcept { return head_ - tail_ == N; }

    template <typename... Args>
    bool push(Args&&... args) {
        if (full()) return false;
        new (storage_ + sizeof(T) * (head_ & mask_)) T(std::forward<Args>(args)...);
        ++head_;
        return true;
    }

    bool pop(T& out) {
        if (empty()) return false;
        T* p = slot(tail_);
        out = std::move(*p);
        if constexpr (!std::is_trivially_destructible_v<T>)
            p->~T();   // manual destructor — storage stays valid
        ++tail_;
        return true;
    }
};

void demo_placement_ring() {
    std::cout << "--- PlacementRing (reuse storage, zero heap) ---\n";
    PlacementRing<Widget, 4> ring;

    ring.push(1, 1.1);
    ring.push(2, 2.2);
    ring.push(3, 3.3);

    Widget w{0, 0};
    while (ring.pop(w)) {
        std::cout << "  Popped: id=" << w.id << ", value=" << w.value << "\n";
    }
    std::cout << "  (all storage lives inside the ring object — no heap)\n\n";
}

// ─── 5. aligned_storage — pre-C++23 alternative ──────────────────────────────

// C++23 deprecates aligned_storage; use alignas + char array instead.
// Showing both for reference:

void demo_aligned_storage() {
    std::cout << "--- Aligned Storage Patterns ---\n";

    // Modern (C++17+): char array + alignas
    alignas(Widget) char modern_buf[sizeof(Widget)];
    Widget* p = new (modern_buf) Widget{77, 7.7};
    std::cout << "  modern buf: id=" << p->id << "\n";
    p->~Widget();

    // Pre-C++23: std::aligned_storage (deprecated in C++23)
    // std::aligned_storage_t<sizeof(Widget), alignof(Widget)> old_buf;
    // Widget* q = new (&old_buf) Widget{88, 8.8};
    // Prefer the modern approach above.

    std::cout << "  Use: alignas(T) char buf[sizeof(T)];\n\n";
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Placement New and Manual Lifecycle ===\n\n";
    demo_placement_new_basics();
    demo_launder();
    demo_trivial_copy();
    demo_placement_ring();
    demo_aligned_storage();

    std::cout << "=== Rules ===\n"
              << "  1. placement new = construct in existing storage (no alloc).\n"
              << "  2. ptr->~T() = destroy without freeing storage.\n"
              << "  3. Always std::launder after placement-new over old storage.\n"
              << "  4. memcpy is legal iff is_trivially_copyable_v<T>.\n"
              << "  5. Use alignas(T) char buf[sizeof(T)] for storage (C++17+).\n";
    return 0;
}
