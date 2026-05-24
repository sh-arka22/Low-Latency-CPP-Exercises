// Chapter 1 / Repo Ch.2  ·  Section 2.8 — RAII / deterministic destruction
//
// std::lock_guard<std::mutex> takes the lock in its constructor and releases
// it in its destructor. Because destructors run on EVERY scope exit (normal,
// early return, exception), the mutex is guaranteed released no matter how
// the function ends.
//
// This is what makes C++ resource management predictable — and is the
// structural reason HFT code is feasible in C++ but not in GC languages.

#include <gtest/gtest.h>
#include <mutex>
#include <stdexcept>

namespace {

auto func(std::mutex& m, int val, bool early_exit) {
    auto guard = std::lock_guard<std::mutex>{m};   // ACQUIRE
    if (early_exit) {
        // guard's destructor releases the mutex here.
        return;
    }
    if (val == 313) {
        // guard's destructor releases the mutex on stack unwind.
        throw std::runtime_error{"boom"};
    }
    // guard's destructor releases the mutex at the closing brace.
}

} // namespace

TEST(RAII, MutexReleasedOnAllExitPaths) {
    std::mutex m;

    // Normal exit
    func(m, 0, false);
    EXPECT_TRUE(m.try_lock()); m.unlock();

    // Early return
    func(m, 0, true);
    EXPECT_TRUE(m.try_lock()); m.unlock();

    // Exception unwind
    EXPECT_THROW(func(m, 313, false), std::runtime_error);
    EXPECT_TRUE(m.try_lock()); m.unlock();
}

// HFT lens: in a hot path you do NOT want std::lock_guard<std::mutex> at all
// (the system call alone is ~5 µs). You'd use a custom spinlock — see the
// SpinBarrier memory entry — but the RAII pattern is identical:
//
//   struct SpinGuard {
//       SpinLock& l_;
//       explicit SpinGuard(SpinLock& l) : l_(l) { l_.lock(); }
//       ~SpinGuard()                          { l_.unlock(); }
//   };
//
// Same scope-based release, ~20 ns instead of ~5000 ns.
