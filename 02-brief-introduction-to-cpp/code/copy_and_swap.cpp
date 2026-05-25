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
#include <cstddef>
#include <memory>
#include <new>       // std::bad_alloc
#include <utility>   // std::swap
#include <vector>

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

// ===== L2.7.b — strong exception guarantee under a throwing allocator ====
//
// We need an allocator we can *arm* to throw on a specific allocation. Then
// we run operator= and confirm that when the second internal copy throws,
// `t0` is bit-for-bit unchanged (no half-merged state) — that's the contract
// copy-and-swap is supposed to give us.
//
// Shared poison state — kept in a non-template struct so a single counter is
// observed by every ThrowingAlloc<T> instantiation (Leaf and Branch share it).
namespace {
struct AllocPoison {
    static inline int trip_after = -1;   // -1 → never throw
    static inline int allocations = 0;

    static void disarm()              { trip_after = -1; allocations = 0; }
    static void arm_after(int n_ok)   { allocations = 0; trip_after = n_ok; }
};

template <typename T>
struct ThrowingAlloc {
    using value_type = T;

    ThrowingAlloc() = default;
    template <typename U> ThrowingAlloc(const ThrowingAlloc<U>&) noexcept {}

    T* allocate(std::size_t n) {
        if (AllocPoison::trip_after >= 0 &&
            ++AllocPoison::allocations > AllocPoison::trip_after) {
            throw std::bad_alloc{};
        }
        return std::allocator<T>{}.allocate(n);
    }
    void deallocate(T* p, std::size_t n) noexcept {
        std::allocator<T>{}.deallocate(p, n);
    }

    template <typename U>
    bool operator==(const ThrowingAlloc<U>&) const noexcept { return true; }
    template <typename U>
    bool operator!=(const ThrowingAlloc<U>&) const noexcept { return false; }
};
} // anonymous namespace

// Same shape as OakTree, just with the poisonable allocator. Keeps the
// existing OakTree (and its test) untouched.
class OakTreeAlloc {
public:
    using LeafVec   = std::vector<Leaf,   ThrowingAlloc<Leaf>>;
    using BranchVec = std::vector<Branch, ThrowingAlloc<Branch>>;

    OakTreeAlloc() = default;
    OakTreeAlloc(LeafVec l, BranchVec b)
        : leafs_(std::move(l)), branches_(std::move(b)) {}

    OakTreeAlloc& operator=(const OakTreeAlloc& other) {
        auto leafs    = other.leafs_;     // allocation #1
        auto branches = other.branches_;  // allocation #2 (we make this throw)
        std::swap(leafs_,    leafs);
        std::swap(branches_, branches);
        return *this;
    }

    LeafVec   leafs_;
    BranchVec branches_;
};

TEST(CopyAndSwap, StrongGuaranteeOnAllocatorThrow) {
    OakTreeAlloc::LeafVec   l0{Leaf{1}, Leaf{2}};
    OakTreeAlloc::BranchVec b0{Branch{1}};
    auto t0 = OakTreeAlloc{std::move(l0), std::move(b0)};

    OakTreeAlloc::LeafVec   l1{Leaf{9}};
    OakTreeAlloc::BranchVec b1{Branch{9}, Branch{8}};
    auto t1 = OakTreeAlloc{std::move(l1), std::move(b1)};

    // Arm: let the leafs copy succeed (1 allocate), make the branches copy throw.
    AllocPoison::arm_after(1);
    EXPECT_THROW(t0 = t1, std::bad_alloc);
    AllocPoison::disarm();

    // The contract: t0 is untouched — original sizes and ids intact.
    ASSERT_EQ(t0.leafs_.size(),    2u);
    ASSERT_EQ(t0.branches_.size(), 1u);
    EXPECT_EQ(t0.leafs_[0].id,    1);
    EXPECT_EQ(t0.leafs_[1].id,    2);
    EXPECT_EQ(t0.branches_[0].id, 1);

    // And the source is untouched too.
    EXPECT_EQ(t1.leafs_.size(),    1u);
    EXPECT_EQ(t1.branches_.size(), 2u);
}
