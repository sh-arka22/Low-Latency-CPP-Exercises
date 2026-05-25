// Chapter 1 / Repo Ch.2  ·  Section 2.6 — Strict class interfaces (BY-VALUE EDITION)
//
// BEST APPROACH FOR HFT: Store Engine by value
//
// Why by-value is best:
// - ZERO heap allocations
// - ZERO pointers
// - ZERO indirection
// - Everything on stack (cache-friendly)
// - Move is just memcpy of bytes (sub-nanosecond)
// - One cacheline contains entire object (Boat + Engine)
//
// Memory layout (OPTIMAL):
// ┌─────────────────────────────────────────┐
// │ STACK: main()                           │
// │                                         │
// │ Boat boat1 (24 bytes, one cacheline)    │
// │ ├─ engine_ (embedded, not pointer!)     │
// │ │  ├─ oil_: float (4 bytes)             │
// │ │  └─ id_: int (4 bytes)                │
// │ └─ length_: float (4 bytes)             │
// │                                         │
// └─────────────────────────────────────────┘
//
// NO HEAP ALLOCATION AT ALL
// Everything is stack-allocated, contiguous, cache-friendly

#include <gtest/gtest.h>
#include <iostream>

class Engine {
public:
    void set_oil_amount(float v) { oil_ = v; }
    float get_oil_amount() const { return oil_; }

    void set_id(int id) { id_ = id; }
    int get_id() const { return id_; }

    // No virtual destructor needed (no heap allocation)

private:
    float oil_{};
    int id_{};
    // Total: 8 bytes (fits in one cacheline with Boat's other members)
};

// ===== APPROACH 2: By-value (Zero allocations, BEST for HFT) =============
namespace strict_value {

class Boat {
public:
    // Constructor: Engine is passed by value, then moved into the member
    // Memory operations at construction:
    //   1. Engine e is constructed on stack (caller's frame)
    //   2. engine_ member is initialized with std::move(e)
    //   3. engine_ is a copy of e's bytes (oil_ and id_)
    //   4. No heap allocation; no pointers
    //
    // Stack layout during construction:
    //   [caller's frame: temporary Engine] → [this->engine_] (copy bytes)
    Boat(Engine e, float l)
        : engine_{std::move(e)}, length_{l} {
        // Memory state after construction:
        // - engine_: embedded Engine object (oil_=0, id_=0)
        // - length_: 100.0
        // - Total size: 24 bytes (4+4+4 + padding + 4)
        // - All on stack, one cacheline
    }

    // Delete copy constructor: prevents accidental copying
    // (We want explicit intent, even though by-value copies are cheap)
    Boat(const Boat&) = delete;

    // Delete copy assignment
    Boat& operator=(const Boat&) = delete;

    // Move constructor is implicitly available
    // When you do: Boat b2 = std::move(b1);
    // What happens:
    //   1. memcpy(b2.engine_, b1.engine_, sizeof(Engine))  (~10 bytes)
    //   2. memcpy(b2.length_, b1.length_, sizeof(float))   (~4 bytes)
    //   3. Total: memcpy of ~24 bytes = sub-nanosecond
    //   4. On modern CPUs with move-semantics optimization, this might
    //      be a register move (even faster)
    Boat(Boat&&) noexcept = default;

    // Move assignment operator
    Boat& operator=(Boat&&) noexcept = default;

    // Deep copy (explicit)
    // When you call: Boat b3 = b2.clone();
    // What happens:
    //   1. Create new Boat on stack with copied Engine
    //   2. Return by value: move constructor copies bytes
    // Cost: ~0 nanoseconds (compile-time optimization likely inlines this)
    Boat clone() const {
        return Boat{engine_, length_};
    }

    // Accessors
    void set_length(float l) { length_ = l; }
    Engine& get_engine() { return engine_; }
    const Engine& get_engine() const { return engine_; }

private:
    Engine engine_;   // EMBEDDED, not pointer
    float length_{};
};

} // namespace strict_value

// ===== TESTS ===============================================================

TEST(StrictInterfacesValue, ConstructionZeroAllocations) {
    // Stack allocation only:
    // Boat boat (24 bytes)
    // └─ contains Engine (8 bytes) + length (4 bytes) + padding
    //
    // Heap allocations: 0
    // Pointers: 0
    // Indirection: 0
    auto boat = strict_value::Boat{Engine{}, 100.0f};

    EXPECT_FLOAT_EQ(boat.get_engine().get_oil_amount(), 0.f);
    EXPECT_EQ(boat.get_engine().get_id(), 0);
}

TEST(StrictInterfacesValue, MoveSemanticsBytesCopy) {
    auto boat1 = strict_value::Boat{Engine{}, 100.0f};
    boat1.get_engine().set_oil_amount(3.5f);
    boat1.get_engine().set_id(42);

    // Move operation: memcpy bytes from boat1 to boat2
    // Before move:
    //   boat1 stack: [engine{oil:3.5, id:42}, length:100.0]
    //   boat2 stack: [uninitialized]
    // After move:
    //   boat1 stack: [engine{oil:3.5, id:42}, length:100.0] (still here, moved-from)
    //   boat2 stack: [engine{oil:3.5, id:42}, length:100.0] (copy of bytes)
    //
    // Cost: ~20 bytes memcpy = sub-nanosecond (likely in-register)
    // No heap allocation, no pointers dereferenced
    auto boat2 = std::move(boat1);

    // boat2 now has independent copy of Engine
    EXPECT_FLOAT_EQ(boat2.get_engine().get_oil_amount(), 3.5f);
    EXPECT_EQ(boat2.get_engine().get_id(), 42);
}

TEST(StrictInterfacesValue, CloneIsCheap) {
    auto boat1 = strict_value::Boat{Engine{}, 100.0f};
    boat1.get_engine().set_oil_amount(5.0f);
    boat1.get_engine().set_id(10);

    // Clone: just create a new Boat with same values
    // Since Engine is by-value, cloning is implicit:
    // return Boat{engine_, length_};
    // This constructs a new Boat with a copy of engine_'s bytes
    //
    // Cost: Constructor call, which just copies bytes (~0 ns with optimization)
    auto boat2 = boat1.clone();

    // boat2 has independent copy of Engine
    boat2.get_engine().set_oil_amount(10.0f);

    // boat1's engine is unchanged
    EXPECT_FLOAT_EQ(boat1.get_engine().get_oil_amount(), 5.0f);
    EXPECT_FLOAT_EQ(boat2.get_engine().get_oil_amount(), 10.0f);
}

TEST(StrictInterfacesValue, CacheLayout) {
    // This is the KEY advantage for HFT
    //
    // Cache line size: 64 bytes (on most x86-64 CPUs)
    //
    // By-value Boat:
    //   Byte layout: [oil:4][id:4][length:4][padding:4] = 16 bytes
    //   → Fits in ONE cacheline (with room to spare)
    //   → When you iterate vector<Boat>, you get linear prefetch
    //   → Cache hit rate: ~99% on sequential access
    //
    // Pointer-based Boat:
    //   Boat on stack: [engine_ptr:8][length:4][padding:4] = 16 bytes
    //   Engine on heap: [oil:4][id:4] = 8 bytes (on different cache line)
    //   → Two cacheline accesses per iteration
    //   → Cache miss on engine_ dereference
    //   → Cache hit rate: ~50% on sequential access
    //
    // For a hot loop with 1M iterations:
    //   By-value:   1M cache hits
    //   Pointer:    ~500K cache hits, ~500K misses
    //   Difference: ~100-500 nanoseconds per miss × 500K = 50-250 milliseconds
    //   That's latency you can't afford in HFT!

    // Test: create many boats and access in sequence
    std::vector<strict_value::Boat> boats;
    for (int i = 0; i < 10; ++i) {
        boats.push_back(strict_value::Boat{Engine{}, static_cast<float>(i)});
    }

    // Linear iteration: cache-friendly
    float total_oil = 0.0f;
    for (const auto& boat : boats) {
        total_oil += boat.get_engine().get_oil_amount();
    }

    // This loop is cache-optimal because everything is contiguous on stack
    EXPECT_FLOAT_EQ(total_oil, 0.0f);
}

TEST(StrictInterfacesValue, NoPointerChasing) {
    // Accessing members is direct (no pointer dereference)
    auto boat = strict_value::Boat{Engine{}, 100.0f};

    // boat.get_engine() returns Engine& directly
    // No pointer dereference involved
    // CPU does: read from [RBP - offset] where offset is known at compile-time
    // This is an L1 cache hit in ~1-2 cycles

    boat.get_engine().set_oil_amount(42.0f);
    EXPECT_FLOAT_EQ(boat.get_engine().get_oil_amount(), 42.0f);
}

TEST(StrictInterfacesValue, VectorOfBoats) {
    // Memory layout of vector<Boat> with by-value Boats:
    //   [Boat0: engine{...}, length][Boat1: engine{...}, length][...]
    //   ↑ Contiguous on heap
    //   ↑ Perfect for cache prefetch
    //   ↑ Ideal for hot-path order book iteration

    std::vector<strict_value::Boat> order_book;
    order_book.reserve(1000);

    for (int i = 0; i < 100; ++i) {
        auto engine = Engine{};
        engine.set_id(i);
        order_book.push_back(strict_value::Boat{engine, 100.0f + i});
    }

    // Iteration: linear access, cache-optimal
    int count = 0;
    for (const auto& boat : order_book) {
        count += boat.get_engine().get_id();
    }

    // Sum of 0..99 = 4950
    EXPECT_EQ(count, 4950);
}

