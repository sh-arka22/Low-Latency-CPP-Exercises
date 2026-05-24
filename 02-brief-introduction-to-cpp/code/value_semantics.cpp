// Chapter 1 / Repo Ch.2  ·  Section 2.3 — Value semantics
//
// Passing by value MOVES OWNERSHIP into the Bagel. Mutating the original
// set after construction does NOT affect any Bagel already built from it.
// The same code in Java would silently alias.

#include <gtest/gtest.h>
#include <set>
#include <string>
#include <utility>  // std::move

class Bagel {
public:
    // Pass-by-value + std::move = "sink" idiom.
    // When the caller passes an lvalue: 1 copy.
    // When the caller passes an rvalue: 0 copies (move).
    explicit Bagel(std::set<std::string> ts)
        : toppings_(std::move(ts)) {}

    const std::set<std::string>& toppings() const { return toppings_; }

private:
    std::set<std::string> toppings_;
};

TEST(ValueSemantics, BagelDoesNotAliasOriginalSet) {
    auto t = std::set<std::string>{};
    t.insert("salt");

    auto a = Bagel{t};
    EXPECT_TRUE(a.toppings().contains("salt"));
    EXPECT_FALSE(a.toppings().contains("pepper"));

    // Mutating t does NOT touch bagel a.
    t.insert("pepper");
    EXPECT_FALSE(a.toppings().contains("pepper"));

    auto b = Bagel{t};
    EXPECT_TRUE(b.toppings().contains("salt"));
    EXPECT_TRUE(b.toppings().contains("pepper"));

    t.insert("oregano");
    EXPECT_FALSE(a.toppings().contains("oregano"));
    EXPECT_FALSE(b.toppings().contains("oregano"));
}

// TODO (L2.3.c): change the parameter to `const std::set<std::string>&` and
// remove the std::move. Show that the behaviour is identical, then argue why
// the by-value+move version is strictly better when the callee STORES the
// argument (the "sink" idiom — see Meyers EMC++ item 41).
