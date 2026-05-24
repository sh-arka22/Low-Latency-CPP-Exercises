// Chapter 1 / Repo Ch.2  ·  Section 2.6 — Strict class interfaces
//
// The Boat/Engine bug: a class that holds shared_ptr is trivially copyable
// by default, but copies SILENTLY ALIAS the engine. Mutation through one
// copy is visible through every other copy.
//
// Fix: make the class non-copyable (or move-only) and provide an explicit
// `clone()` if deep copy is what you want.

#include <gtest/gtest.h>
#include <memory>

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
namespace loose {
class Boat {
public:
    Boat(std::shared_ptr<Engine> e, float l)
        : engine_{std::move(e)}, length_{l} {}

    auto set_length(float l)  { length_ = l; }
    auto& get_engine()        { return engine_; }

private:
    std::shared_ptr<Engine> engine_;
    float length_{};
};
} // namespace loose

// ===== Version 2: strict interface (CORRECT) =============================
namespace strict {
class Boat {
public:
    Boat(std::shared_ptr<Engine> e, float l)
        : engine_{std::move(e)}, length_{l} {}

    // Non-copyable: the only way to "duplicate" a Boat is via clone().
    Boat(const Boat&)            = delete;
    Boat& operator=(const Boat&) = delete;

    // Move ops are still implicitly available.
    Boat(Boat&&)            noexcept = default;
    Boat& operator=(Boat&&) noexcept = default;

    // Explicit deep copy — caller must ask for it by name.
    Boat clone() const {
        auto e = std::make_shared<Engine>(*engine_);   // deep-copy the engine
        return Boat{std::move(e), length_};
    }

    auto set_length(float l)  { length_ = l; }
    auto& get_engine()        { return engine_; }

private:
    std::shared_ptr<Engine> engine_;
    float length_{};
};
} // namespace strict

// ===== Tests =============================================================

TEST(StrictInterfaces, LooseBoatSilentlyAliases) {
    auto boat0 = loose::Boat{std::make_shared<YamahaEngine>(), 6.7f};
    auto boat1 = boat0;                  // copy compiles — but aliases engine
    boat1.get_engine()->set_oil_amount(3.4f);

    // BUG: the "copy" mutates the original.
    EXPECT_FLOAT_EQ(boat0.get_engine()->get_oil_amount(), 3.4f);
}

TEST(StrictInterfaces, StrictBoatRefusesToCopy) {
    auto boat0 = strict::Boat{std::make_shared<YamahaEngine>(), 6.7f};
    // auto boat1 = boat0;               // ← won't compile. Try uncommenting.
    auto boat1 = boat0.clone();          // explicit deep copy
    boat1.get_engine()->set_oil_amount(3.4f);

    // FIXED: the clone is fully independent.
    EXPECT_FLOAT_EQ(boat0.get_engine()->get_oil_amount(), 0.f);
}
