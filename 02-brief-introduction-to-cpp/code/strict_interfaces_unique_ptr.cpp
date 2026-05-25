// Chapter 1 / Repo Ch.2  ·  Section 2.6 — Strict class interfaces (UNIQUE_PTR EDITION)
//
// IMPROVED VERSION: Using unique_ptr<Engine> instead of shared_ptr
//
// Why unique_ptr is better for HFT:
// - No atomic reference counting
// - Move is a simple pointer swap (~1 nanosecond)
// - Single ownership is explicit at the type level
// - No contention on multi-threaded paths
//
// Memory layout:
// ┌─────────────────────────────────────────┐
// │ STACK: main()                           │
// │                                         │
// │ Boat boat1                              │
// │ ├─ engine_: unique_ptr                  │ ──┐
// │ │  (just a pointer, 8 bytes)            │   │
// │ └─ length_: float (4 bytes)             │   │
// │                                         │   │
// └─────────────────────────────────────────┘   │
//                                                │
//  ┌────────────────────────────────────────┐   │
//  │ HEAP: allocated memory                 │ <─┘
//  │                                        │
//  │ Engine object (owned by boat1)         │
//  │ ├─ oil_: float (4 bytes)               │
//  │ └─ vptr: (if virtual, 8 bytes)         │
//  │                                        │
//  └────────────────────────────────────────┘

#include <gtest/gtest.h>
#include <memory>
#include <iostream>

class Engine {
public:
    void set_oil_amount(float v) { oil_ = v; }
    float get_oil_amount() const { return oil_; }

    // Virtual destructor for polymorphism
    virtual ~Engine() = default;

private:
    float oil_{};
};

class YamahaEngine : public Engine {};

// ===== APPROACH 1: unique_ptr (Single ownership, zero-cost) ==============
namespace strict_unique {

class Boat {
public:
    // Constructor: takes ownership of the Engine via unique_ptr
    // At construction time:
    // 1. The unique_ptr parameter is moved into engine_
    // 2. Ownership is transferred from the caller to this Boat
    // 3. When this Boat is destroyed, it automatically deletes the Engine
    Boat(std::unique_ptr<Engine> e, float l)
        : engine_{std::move(e)}, length_{l} {
        // Memory state after construction:
        // - engine_: points to Engine on heap
        // - length_: 100.0
        // - Total size: 16 bytes (8-byte ptr + 4-byte float + 4 padding)
    }

    // Delete copy constructor: prevents silent aliasing
    // If someone tries: Boat b2 = b1;
    // The compiler will reject it with an error.
    Boat(const Boat&) = delete;

    // Delete copy assignment: same reason
    // If someone tries: b2 = b1;
    // The compiler will reject it with an error.
    Boat& operator=(const Boat&) = delete;

    // Move constructor is implicitly available
    // When you do: Boat b2 = std::move(b1);
    // What happens:
    //   1. b2.engine_ gets b1.engine_ (pointer value copied)
    //   2. b1.engine_ is set to nullptr (b1 is now empty)
    //   3. Only one unique_ptr "owns" the Engine at any time
    //   4. Cost: just a pointer assignment (~1 nanosecond)
    Boat(Boat&&) noexcept = default;

    // Move assignment operator: same as move constructor
    Boat& operator=(Boat&&) noexcept = default;

    // Explicit deep copy: caller must ask by name
    // When you call: Boat b3 = b2.clone();
    // What happens:
    //   1. *engine_ dereferences the pointer to get the original Engine
    //   2. std::make_unique<Engine>(*engine_) allocates a NEW Engine on heap
    //   3. The new Engine is copy-constructed from the original
    //   4. A new Boat is created, owning the new Engine
    //   5. Return by value: new Boat is move-constructed into b3
    // Cost: one heap allocation + copy constructor
    Boat clone() const {
        std::unique_ptr<Engine> e = std::make_unique<Engine>(*engine_);
        return Boat{std::move(e), length_};
    }

    // Accessors
    void set_length(float l) { length_ = l; }

    /**
     * @brief Returns a reference to the underlying Engine instance.
     *
     * This accessor provides direct, non-owning access to the Engine object
     * managed by the unique pointer `engine_`. The caller receives a reference,
     * allowing them to interact with the Engine without transferring ownership.
     * 
     * @return Engine& Reference to the managed Engine instance.
     * @note Use with caution: since this returns a reference, ensure that the
     *       lifetime of the Engine object exceeds that of the reference.
     */
    Engine& get_engine() { return *engine_; }
    const Engine& get_engine() const { return *engine_; }

    int a = 10;  // Just to show that we can have other members alongside the unique_ptr
    int* b = new int(10);  // This is just to demonstrate that we can have pointers as members, but it's not recommended in practice
    int& c = *b;
    int d = *b;  // This is just to show that we can have other pointers as members, but it's not recommended in practice

private:
    std::unique_ptr<Engine> engine_;  // Single owner of Engine
    float length_{};
};

} // namespace strict_unique

// ===== TESTS ===============================================================

TEST(StrictInterfacesUnique, ConstructionOwnership) {
    // Allocation: 1 heap object (Engine)
    // Stack: 1 Boat object (16 bytes)
    // Memory layout:
    // Stack (main): [Boat: engine_ptr, length] (16 bytes)
    //                         |
    //                         v
    // Heap: [Engine object] (16 bytes)
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    // auto boat = Engine();  // ← This would be a compile error: no implicit conversion from Engine to unique_ptr<Engine>
    strict_unique::Boat boat{std::move(engine), 100.0f};

    // At this point:
    // - boat owns the Engine
    // - engine is nullptr (moved-from)
    EXPECT_FLOAT_EQ(boat.get_engine().get_oil_amount(), 0.f);
}

TEST(StrictInterfacesUnique, MoveSemantics) {
    strict_unique::Boat boat1 = strict_unique::Boat{std::make_unique<Engine>(), 100.0f};

    // Set engine oil on boat1
    boat1.get_engine().set_oil_amount(3.5f);

    // Move operation: transfer ownership from boat1 to boat2
    // Cost: ~1 nanosecond (just pointer swap)
    // Before move:
    //   boat1.engine_ → [Engine with oil=3.5]
    //   boat2.engine_ → nullptr
    // After move:
    //   boat1.engine_ → nullptr (moved-from, empty)
    //   boat2.engine_ → [Engine with oil=3.5]
    auto boat2 = std::move(boat1);

    // boat2 now owns the Engine
    EXPECT_FLOAT_EQ(boat2.get_engine().get_oil_amount(), 3.5f);

    // boat1 is moved-from; we shouldn't use it
    // (In production, it would go out of scope and destroy cleanly)
}

TEST(StrictInterfacesUnique, CloneCreatesIndependentCopy) {
    auto boat1 = strict_unique::Boat{std::make_unique<Engine>(), 100.0f};
    boat1.get_engine().set_oil_amount(5.0f);

    // Clone: deep copy of the Engine
    // What happens:
    //   1. boat1.engine_ points to Engine #1 (oil=5.0)
    //   2. clone() calls std::make_unique<Engine>(*boat1.engine_)
    //   3. NEW Engine #2 is allocated on heap
    //   4. Engine #2 is copy-constructed from Engine #1 (oil=5.0)
    //   5. boat2 owns Engine #2, boat1 still owns Engine #1
    // Cost: ~1 microsecond (heap allocation + copy constructor)
    // Result: two independent engines
    auto boat2 = boat1.clone();

    // Mutate boat2's engine
    boat2.get_engine().set_oil_amount(10.0f);

    // boat1's engine is unchanged (different object on heap)
    EXPECT_FLOAT_EQ(boat1.get_engine().get_oil_amount(), 5.0f);
    EXPECT_FLOAT_EQ(boat2.get_engine().get_oil_amount(), 10.0f);
}

TEST(StrictInterfacesUnique, CopyForbidden) {
    auto boat1 = strict_unique::Boat{std::make_unique<Engine>(), 100.0f};

    // This code will NOT compile:
    // auto boat2 = boat1;  // ← Compiler error: deleted copy constructor

    // To verify the fix, we test that move works:
    auto boat2 = std::move(boat1);  // ← This compiles

    EXPECT_FLOAT_EQ(boat2.get_engine().get_oil_amount(), 0.f);
}

TEST(StrictInterfacesUnique, NoAtomicOverhead) {
    // Key advantage over shared_ptr:
    // - Move is a simple pointer assignment, no atomics
    // - No ref-counting overhead
    // - On multi-threaded hot paths, this matters:
    //
    //   shared_ptr move:
    //     atomic_increment(&ref_count)     // ← cacheline bounce
    //     memory_fence()                   // ← waiting for fence
    //     atomic_decrement(&old_ref_count) // ← another cacheline bounce
    //
    //   unique_ptr move:
    //     engine_ = other.engine_;         // ← one write, done
    //
    // This test just documents the property; the actual performance
    // would be measured with a benchmark.

    auto boat1 = strict_unique::Boat{std::make_unique<Engine>(), 100.0f};
    auto boat2 = std::move(boat1);

    // No test assertion needed; this is documented behavior
}

