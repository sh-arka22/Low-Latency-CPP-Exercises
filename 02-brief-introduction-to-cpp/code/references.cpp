// Chapter 1 / Repo Ch.2  ·  Section 2.5 — References vs pointers in signatures
//
// `const T&` ⇒ object is REQUIRED, callee guaranteed non-null.
// `const T*` ⇒ object is OPTIONAL, callee MUST handle null.
// The signature itself becomes the documentation. No annotations needed.

#include <gtest/gtest.h>
#include <cmath>
#include <memory>  // std::addressof

struct Sphere {
    auto radius() const { return 0.05f; }
};

// Cannot be passed nullptr — won't compile.
auto get_volume1(const Sphere& s) {
    auto cube = std::pow(s.radius(), 3);
    constexpr auto pi = 3.14159265f;
    return (pi * 4.f / 3.f) * cube;
}

// Pointer ⇒ caller may pass nullptr, callee MUST check.
auto get_volume2(const Sphere* s) {
    auto rad  = s ? s->radius() : 0.f;
    auto cube = std::pow(rad, 3);
    constexpr auto pi = 3.14159265f;
    return (pi * 4.f / 3.f) * cube;
}

TEST(References, RefSignatureRejectsNull) {
    auto s = Sphere{};
    EXPECT_GT(get_volume1(s), 0.f);
    // get_volume1(nullptr);  // ← won't compile. Try uncommenting.
}

TEST(References, PointerSignatureAllowsNull) {
    auto s   = Sphere{};
    auto sp  = std::addressof(s);
    EXPECT_GT(get_volume2(sp),       0.f);
    EXPECT_EQ(get_volume2(nullptr), 0.f);
}

// TODO (L2.5.c): rewrite get_volume2 to use
//     std::optional<std::reference_wrapper<const Sphere>>
// and compare clarity at the call site.
