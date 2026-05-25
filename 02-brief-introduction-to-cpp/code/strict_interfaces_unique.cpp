// Chapter 1 / Repo Ch.2  ·  Section 2.6 — Strict class interfaces (unique_ptr variant)
//
// Same lesson as strict_interfaces.cpp, different failure mode.
//
// With shared_ptr the loose-interface bug was silent ALIASING on copy.
// unique_ptr removes that bug for free (the class becomes move-only), but
// it exposes a different loose-interface trap: handing out a reference to
// the owning smart pointer lets a caller MOVE the engine out from under the
// Boat, breaking the Boat's "I always own an Engine" invariant.
//
// Fix: return a reference to the OBJECT, not to the owning pointer.

#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>

// Anonymous namespace → internal linkage, avoids ODR collision with the
// shared_ptr version's Engine/YamahaEngine in the same translation unit set.
namespace {

class Engine {
public:
    auto set_oil_amount(float v) { oil_ = v; }
    auto get_oil_amount() const { return oil_; }
    virtual ~Engine() = default;

private:
    float oil_{};
};

class YamahaEngine : public Engine {};

// ===== Version 1: loose interface (BUGGY) ================================
// Owns via unique_ptr (good), but leaks a reference to the unique_ptr
// itself (bad). A caller can std::move the engine out and the Boat's
// invariant silently breaks — engine_ becomes null.
namespace loose {
class Boat {
public:
    Boat(std::unique_ptr<Engine> e, float l)
        : engine_{std::move(e)}, length_{l} {}

    auto  set_length(float l) { length_ = l; }
    auto& get_engine()        { return engine_; }   // ← leaks ownership handle

private:
    std::unique_ptr<Engine> engine_;
    float length_{};
};
} // namespace loose

// ===== Version 2: strict interface (CORRECT) =============================
// Move-only comes free from unique_ptr — no = delete / = default boilerplate.
// get_engine() hands out a reference to the OBJECT, not the owning pointer,
// so the caller can observe and mutate but cannot steal ownership.
namespace strict {
class Boat {
public:
    Boat(std::unique_ptr<Engine> e, float l)
        : engine_{std::move(e)}, length_{l} {}

    // No copy/move boilerplate needed: unique_ptr already makes this
    // class move-only with correct semantics. That's the win over the
    // shared_ptr version, which had to spell out 4 special members.

    // Explicit deep copy — caller must ask for it by name.
    Boat clone() const {
        auto e = std::make_unique<Engine>(*engine_);
        return Boat{std::move(e), length_};
    }

    auto set_length(float l)               { length_ = l; }
    Engine&       get_engine()             { return *engine_; }
    const Engine& get_engine() const       { return *engine_; }

private:
    std::unique_ptr<Engine> engine_;
    float length_{};
};
} // namespace strict

} // anonymous namespace

// ===== Tests =============================================================

// The shared_ptr file demonstrated copy-aliasing. unique_ptr fixes that
// at compile time — this test just pins the fact.
TEST(StrictInterfacesUnique, LooseBoatRefusesToCopy) {
    auto boat0 = loose::Boat{std::make_unique<YamahaEngine>(), 6.7f};
    // auto boat1 = boat0;            // ← won't compile, unique_ptr is non-copyable
    static_assert(!std::is_copy_constructible_v<loose::Boat>);
    static_assert( std::is_move_constructible_v<loose::Boat>);
}

// The NEW bug unique_ptr exposes: leaking the owning handle lets a
// caller silently steal the engine.
TEST(StrictInterfacesUnique, LooseBoatLetsCallerStealEngine) {
    auto boat = loose::Boat{std::make_unique<YamahaEngine>(), 6.7f};
    ASSERT_NE(boat.get_engine().get(), nullptr);

    // Nothing in the API stops this. The Boat's invariant ("I own an
    // Engine") is now broken — engine_ is null.
    auto stolen = std::move(boat.get_engine());

    EXPECT_EQ(boat.get_engine().get(), nullptr);   // BUG: Boat is now engine-less
    EXPECT_NE(stolen.get(), nullptr);              // caller walked off with it
}

// Strict version: get_engine() returns Engine&, so the caller can use
// the engine but cannot move-from or reset the owning pointer.
TEST(StrictInterfacesUnique, StrictBoatProtectsOwnership) {
    auto boat = strict::Boat{std::make_unique<YamahaEngine>(), 6.7f};
    boat.get_engine().set_oil_amount(3.4f);
    EXPECT_FLOAT_EQ(boat.get_engine().get_oil_amount(), 3.4f);

    // auto stolen = std::move(boat.get_engine());   // ← Engine has no move-from-ref escape hatch
    // boat.get_engine().reset();                    // ← won't compile, not a unique_ptr anymore
}

// Clone still works, and is cheaper to write than the shared_ptr version
// because we don't need any explicit special-member declarations.
TEST(StrictInterfacesUnique, StrictBoatCloneIsIndependent) {
    auto boat0 = strict::Boat{std::make_unique<YamahaEngine>(), 6.7f};
    auto boat1 = boat0.clone();
    boat1.get_engine().set_oil_amount(3.4f);

    EXPECT_FLOAT_EQ(boat0.get_engine().get_oil_amount(), 0.f);
    EXPECT_FLOAT_EQ(boat1.get_engine().get_oil_amount(), 3.4f);
}

// Standalone demo — narrates each scenario with its input and observable
// output, so you can read the file top-to-bottom and *see* the difference
// between loose and strict interfaces at runtime.
//
// Guarded so the CMake build (which already links main.cpp's main) is
// unaffected. Compile this file on its own with:
//
//   clang++ -std=c++20 -DSTRICT_INTERFACES_UNIQUE_STANDALONE \
//       -I/usr/local/include strict_interfaces_unique.cpp \
//       /usr/local/lib/libgtest.a -o sui && ./sui
//
#ifdef STRICT_INTERFACES_UNIQUE_STANDALONE

namespace {
void hr(const char* title) {
    std::cout << "\n=== " << title << " ===\n";
}
} // namespace

int main() {
    // --- Scenario 1: loose Boat is move-only (unique_ptr fixed the
    //                 shared_ptr aliasing bug for free) ------------------
    hr("Scenario 1: loose::Boat refuses to copy");
    std::cout << "input : auto b1 = b0;   // attempt to copy\n"
              << "output: compile error — unique_ptr is non-copyable\n"
              << "        (verified at compile time via static_assert below)\n";
    static_assert(!std::is_copy_constructible_v<loose::Boat>);
    static_assert( std::is_move_constructible_v<loose::Boat>);
    std::cout << "        is_copy_constructible_v<loose::Boat> = "
              << std::is_copy_constructible_v<loose::Boat> << "\n"
              << "        is_move_constructible_v<loose::Boat> = "
              << std::is_move_constructible_v<loose::Boat> << "\n";

    // --- Scenario 2: loose Boat leaks its owning handle ----------------
    hr("Scenario 2: loose::Boat lets the caller steal the engine");
    {
        auto boat = loose::Boat{std::make_unique<YamahaEngine>(), 6.7f};
        std::cout << "input : auto stolen = std::move(boat.get_engine());\n";
        std::cout << "before: boat.engine() is "
                  << (boat.get_engine() ? "non-null" : "NULL") << "\n";

        auto stolen = std::move(boat.get_engine());   // legal — and a bug

        std::cout << "after : boat.engine() is "
                  << (boat.get_engine() ? "non-null" : "NULL")
                  << "   ← invariant BROKEN\n"
                  << "        stolen.get() = " << stolen.get()
                  << " (caller walked off with the engine)\n";
    }

    // --- Scenario 3: strict Boat protects ownership --------------------
    hr("Scenario 3: strict::Boat exposes the object, not the owning ptr");
    {
        auto boat = strict::Boat{std::make_unique<YamahaEngine>(), 6.7f};
        std::cout << "input : boat.get_engine().set_oil_amount(3.4)\n";
        boat.get_engine().set_oil_amount(3.4f);
        std::cout << "output: oil = " << boat.get_engine().get_oil_amount()
                  << "   (caller can mutate, but cannot move-from or reset)\n";
        // The following lines would not compile — there's no unique_ptr
        // exposed for the caller to hijack:
        //   auto stolen = std::move(boat.get_engine()); // type is Engine&
        //   boat.get_engine().reset();                  // Engine has no reset()
    }

    // --- Scenario 4: clone() produces independent state ----------------
    hr("Scenario 4: strict::Boat::clone() is a deep copy");
    {
        auto boat0 = strict::Boat{std::make_unique<YamahaEngine>(), 6.7f};
        auto boat1 = boat0.clone();
        std::cout << "input : boat1 = boat0.clone();\n"
                  << "        boat1.get_engine().set_oil_amount(3.4)\n";
        boat1.get_engine().set_oil_amount(3.4f);
        std::cout << "output: boat0 oil = " << boat0.get_engine().get_oil_amount()
                  << "   (untouched)\n"
                  << "        boat1 oil = " << boat1.get_engine().get_oil_amount()
                  << "   (mutated independently)\n";
    }

    std::cout << "\nAll scenarios done.\n";
    return 0;
}
#endif
