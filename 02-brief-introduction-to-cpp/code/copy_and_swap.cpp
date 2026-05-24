// Chapter 1 / Repo Ch.2  ·  Section 2.7 — Copy-and-swap (strong exception
// guarantee).
//
// Naive `operator=` copies field-by-field. If the second copy throws, the
// object is left half-modified — invariants violated.
//
// copy-and-swap: do all throwing work on LOCAL copies first, then commit
// to *this with no-throw swap. Either the assignment succeeds completely
// or *this is bit-for-bit unchanged.

#include <gtest/gtest.h>
#include <vector>
#include <utility>   // std::swap

struct Leaf   { int id{}; };
struct Branch { int id{}; };

class OakTree {
public:
    OakTree() = default;
    OakTree(std::vector<Leaf> l, std::vector<Branch> b)
        : leafs_(std::move(l)), branches_(std::move(b)) {}

    // Strong exception guarantee via copy-and-swap.
    OakTree& operator=(const OakTree& other) {
        // 1) Do all the throwing work on LOCAL copies. *this untouched.
        auto leafs    = other.leafs_;
        auto branches = other.branches_;

        // 2) Commit — swap is noexcept on vector<T>.
        std::swap(leafs_,    leafs);
        std::swap(branches_, branches);
        return *this;
    }

    std::vector<Leaf>   leafs_;
    std::vector<Branch> branches_;
};

TEST(CopyAndSwap, NormalAssignment) {
    auto t0 = OakTree({Leaf{1}, Leaf{2}}, {Branch{1}});
    auto t1 = OakTree({Leaf{9}},          {Branch{9}, Branch{8}});
    t0 = t1;
    EXPECT_EQ(t0.leafs_.size(),    1u);
    EXPECT_EQ(t0.branches_.size(), 2u);
    EXPECT_EQ(t0.leafs_[0].id,     9);
}

// TODO (L2.7.b): inject a throwing allocator into one of the copies and
// verify that t0 retains its ORIGINAL contents (not a half-merged state).
