// Chapter 1 / Repo Ch.2  ·  Section 2.4 — Const correctness
//
// `const` on a member function is a compile-time PROMISE that the function
// will not mutate the object. Const-overloading lets one accessor return a
// mutable view to mutable callers and an immutable view to const callers.

#include <gtest/gtest.h>
#include <vector>

class Person {
public:
    auto age() const { return age_; }      // const ⇒ promises not to mutate
    auto set_age(int age) { age_ = age; }  // non-const ⇒ may mutate

private:
    int age_{};
};

class Team {
public:
    auto& leader() const { return leader_; } // const overload — returns const&
    auto& leader()       { return leader_; } // non-const overload — returns ref

private:
    Person leader_{};
};

// teams is const ⇒ compiler enforces no mutation of any team inside.
auto nonmutating_func(const std::vector<Team>& teams) {
    auto tot_age = 0;
    for (const auto& team : teams)
        tot_age += team.leader().age();   // OK: both leader() const and age() const

    // The next line would FAIL to compile:
    //   for (auto& team : teams) team.leader().set_age(20);
    // Reason: `teams` is const, so `team` is const Team&, so `team.leader()`
    // returns const Person&, and set_age is non-const.
    return tot_age;
}

auto mutating_func(std::vector<Team>& teams) {
    for (auto& team : teams)
        team.leader().set_age(20);
}

TEST(ConstCorrectness, ReadOnlyView) {
    const auto teams = std::vector<Team>(3);
    EXPECT_EQ(nonmutating_func(teams), 0);
}

TEST(ConstCorrectness, MutationAllowed) {
    auto teams = std::vector<Team>(3);
    mutating_func(teams);
    for (const auto& t : teams)
        EXPECT_EQ(t.leader().age(), 20);
}

// TODO (L2.4.b): uncomment the offending line in nonmutating_func and READ
//                the compiler diagnostic. It is the most precise piece of
//                documentation the language can give you.
// TODO (L2.4.c): then add `const_cast<Person&>(team.leader()).set_age(20)`
//                and explain WHY that compiles but is a footgun
//                (UB if the underlying Team was actually declared const).
