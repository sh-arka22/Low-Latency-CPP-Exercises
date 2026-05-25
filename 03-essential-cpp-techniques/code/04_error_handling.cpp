/**
 * Chapter 2 — Essential C++ Techniques
 * Topic 4: Error Handling — Exception Safety & Copy-and-Swap
 *
 * ── FIRST PRINCIPLES ──────────────────────────────────────────────────────────
 *
 * FOUR EXCEPTION SAFETY GUARANTEES (strongest → weakest):
 *
 *   1. No-throw guarantee   — function NEVER throws (mark `noexcept`)
 *                             e.g. swap, move constructor, destructor
 *
 *   2. Strong guarantee     — if exception is thrown, state is UNCHANGED
 *                             (like a DB transaction: all-or-nothing)
 *                             Implementation pattern: copy-and-swap
 *
 *   3. Basic guarantee      — if exception is thrown, object is in a VALID
 *                             (though possibly changed) state. No resource leaks.
 *
 *   4. No guarantee         — undefined behavior on exception (avoid)
 *
 * COPY-AND-SWAP IDIOM:
 *   Instead of modifying state directly (and potentially half-finishing on throw):
 *     1. Make copies of the new values (may throw — safe, old state intact)
 *     2. swap() the copies with the member variables (noexcept)
 *   Result: if step 1 throws, original object is unchanged → strong guarantee.
 *
 * HFT NOTE:
 *   Exceptions add overhead when thrown (they involve stack unwinding).
 *   In ultra-low-latency paths: use error codes / std::expected instead.
 *   But for initialization and control paths: exceptions + noexcept contracts
 *   are the right tool. The `noexcept` keyword is crucial — STL optimises
 *   containers (vector realloc, sort) when move is noexcept.
 */

#include <cassert>
#include <cstdio>     // FILE*, fopen, fclose
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// ─── SECTION 1: Basic exception safety levels ─────────────────────────────────

struct Number {
    int i_ = 0;
    bool operator==(const Number& rhs) const { return i_ == rhs.i_; }
    bool operator!=(const Number& rhs) const { return !(*this == rhs); }
};

// ── BAD: no exception safety (basic guarantee at best) ──
class WidgetUnsafe {
public:
    WidgetUnsafe(Number x, Number y) : x_{x}, y_{y} { assert(x_ != y_); }

    void update_bad(Number x, Number y) {
        // If the assignment of y_ throws after x_ was already changed,
        // the object is in an invalid state (x_ == y_ violates invariant)
        x_ = x;   // modifies state
        y_ = y;   // if THIS throws → invariant broken, x_ already changed!
    }

    // ── L2 fix: strong exception guarantee on the same class ────────────
    // All throwing work happens on locals; commit is a no-throw assign of
    // already-validated values. If either local copy throws, *this stays
    // bit-for-bit unchanged.
    void update_strong(const Number& x, const Number& y) {
        auto x_tmp = x;          // may throw — *this untouched
        auto y_tmp = y;          // may throw — *this untouched
        // Number assignment is trivial → noexcept commit.
        static_assert(std::is_nothrow_copy_assignable_v<Number>);
        x_ = x_tmp;
        y_ = y_tmp;
        assert(x_ != y_);
    }

private:
    Number x_, y_;
};

// ── GOOD: strong guarantee via copy-and-swap ─────────────────────────────────
class Widget {
public:
    Widget(Number x, Number y) : x_{x}, y_{y} {
        assert(is_valid());
    }

    // Strong guarantee: either fully succeeds or state is unchanged
    void update(const Number& x, const Number& y) {
        assert(x != y);         // precondition
        assert(is_valid());

        // Step 1: Make copies (can throw — Widget is still untouched)
        auto x_tmp = x;         // copy — might throw
        auto y_tmp = y;         // copy — might throw

        // Step 2: swap — noexcept, cannot fail
        std::swap(x_tmp, x_);
        std::swap(y_tmp, y_);

        assert(is_valid());     // postcondition
    }

    bool is_valid() const { return x_ != y_; }

    void print() const {
        std::cout << "Widget(" << x_.i_ << ", " << y_.i_ << ")\n";
    }

private:
    Number x_, y_;
};

void section_exception_safety() {
    std::cout << "\n=== Exception Safety ===\n";
    auto w = Widget{{1}, {2}};
    w.print();
    w.update({3}, {4});
    w.print();
}

// ─── SECTION 2: noexcept — the contract ──────────────────────────────────────

// If you mark a function noexcept and it throws → std::terminate() is called.
// This is INTENTIONAL: you are promising no exception can escape.
// It allows the compiler to:
//   a) skip exception table generation (smaller binary)
//   b) use move semantics in STL containers (std::vector realloc path)

void no_throw_swap(int& a, int& b) noexcept {
    int tmp = a;
    a = b;
    b = tmp;
}

// Move ops should ALWAYS be noexcept so vector uses them during realloc
class OrderBook {
public:
    OrderBook() = default;
    OrderBook(OrderBook&&) noexcept = default;             // ✓ vector can move-reallocate
    OrderBook& operator=(OrderBook&&) noexcept = default;  // ✓
    ~OrderBook() = default;

    // If you omit noexcept, std::vector<OrderBook>::push_back
    // will COPY instead of MOVE during reallocation → O(n) overhead
};

void section_noexcept() {
    std::cout << "\n=== noexcept ===\n";
    int a = 10, b = 20;
    no_throw_swap(a, b);
    std::cout << "a=" << a << " b=" << b << "\n"; // a=20 b=10

    std::cout << "OrderBook move noexcept? "
              << std::is_nothrow_move_constructible_v<OrderBook> << "\n"; // 1
}

// ─── SECTION 3: RAII — resource safety through scope ─────────────────────────

// RAII = Resource Acquisition Is Initialization
// Acquire in constructor, release in destructor.
// Destructor always runs → no resource leaks, even on exceptions.

class MutexGuard {
public:
    explicit MutexGuard(bool& lock) : lock_(lock) {
        lock_ = true;
        std::cout << "[MutexGuard] locked\n";
    }
    ~MutexGuard() {
        lock_ = false;
        std::cout << "[MutexGuard] released\n";
    }
    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;

private:
    bool& lock_;
};

void do_work(bool& mutex, bool throw_early) {
    MutexGuard guard(mutex);   // locked here
    if (throw_early) {
        throw std::runtime_error("error during work");
    }
    std::cout << "work done\n";
}   // guard destructor always runs here → mutex always released

void section_raii() {
    std::cout << "\n=== RAII ===\n";
    bool mutex = false;
    try {
        do_work(mutex, true);  // throws
    } catch (const std::exception& e) {
        std::cout << "caught: " << e.what() << "\n";
        std::cout << "mutex released? " << !mutex << "\n"; // true → safe
    }
    do_work(mutex, false);     // normal path
}

// ─── SECTION 4 (L2): FileHandle — RAII wrapper for FILE* ─────────────────────
//
// Owns a FILE* exclusively; ctor opens, dtor fclose's. Non-copyable
// (two owners would double-close), move-only (transfers the handle).
// Move ops are noexcept → safe to store in std::vector with realloc using move.

class FileHandle {
public:
    FileHandle(const char* path, const char* mode)
        : fp_{std::fopen(path, mode)} {
        if (!fp_) throw std::runtime_error(std::string{"fopen failed: "} + path);
    }
    ~FileHandle() { if (fp_) std::fclose(fp_); }

    FileHandle(const FileHandle&)            = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    FileHandle(FileHandle&& other) noexcept
        : fp_{std::exchange(other.fp_, nullptr)} {}
    FileHandle& operator=(FileHandle&& other) noexcept {
        if (this != &other) {
            if (fp_) std::fclose(fp_);
            fp_ = std::exchange(other.fp_, nullptr);
        }
        return *this;
    }

    std::FILE* get() const noexcept { return fp_; }

private:
    std::FILE* fp_{};
};

void section_filehandle() {
    std::cout << "\n=== FileHandle (RAII) ===\n";
    static_assert(std::is_nothrow_move_constructible_v<FileHandle>);
    static_assert(!std::is_copy_constructible_v<FileHandle>);

    // Open a tmp file, write, close-by-scope. No explicit fclose needed.
    {
        FileHandle f{"/tmp/04_filehandle_demo.txt", "w"};
        std::fprintf(f.get(), "FileHandle owns me\n");
    }   // ← fclose runs here automatically
    std::cout << "wrote /tmp/04_filehandle_demo.txt and closed via dtor\n";

    // Move transfers ownership; the source is left as a harmless nullptr.
    FileHandle a{"/tmp/04_filehandle_demo.txt", "r"};
    FileHandle b{std::move(a)};
    std::cout << "after move: a.get()==nullptr? " << (a.get() == nullptr)
              << "  b.get() valid? " << (b.get() != nullptr) << "\n";

    // Throwing path: fopen on a non-existent file in a read-only dir.
    try {
        FileHandle bad{"/proc/this/does/not/exist", "r"};
    } catch (const std::exception& e) {
        std::cout << "caught: " << e.what() << "\n";
    }
}

// ─── SECTION 5 (L2): basic guarantee but NOT strong ──────────────────────────
//
// append_all() succeeds in adding *some* items if a copy throws partway.
// The vector is still in a valid state (size reflects what got added, no
// leaks, destructible) — that's the basic guarantee. But it's not strong:
// the caller can't roll back to "as if append_all was never called".
//
// To upgrade to strong: build a local copy `tmp = dst`, append to tmp,
// then `dst.swap(tmp)`. That's left as a one-line contrast below.

void append_all_basic(std::vector<std::string>& dst,
                      const std::vector<std::string>& src) {
    for (const auto& s : src) {
        dst.push_back(s);     // each push_back may throw — earlier ones stay
    }
}

void append_all_strong(std::vector<std::string>& dst,
                       const std::vector<std::string>& src) {
    auto tmp = dst;                                   // copy may throw
    tmp.insert(tmp.end(), src.begin(), src.end());    // may throw
    dst.swap(tmp);                                    // noexcept commit
}

void section_basic_vs_strong() {
    std::cout << "\n=== basic vs strong ===\n";
    std::vector<std::string> dst = {"a", "b"};
    append_all_basic(dst, {"c", "d"});
    std::cout << "basic: dst.size() = " << dst.size() << "\n";   // 4

    // No actual throw here — point is the *guarantee*. If src copies threw
    // partway, append_all_basic would leave dst partially modified;
    // append_all_strong would leave dst exactly as it was.
    append_all_strong(dst, {"e", "f"});
    std::cout << "strong: dst.size() = " << dst.size() << "\n";  // 6
}

// ─── SECTION 6 (L2): verify noexcept on every move op above ──────────────────

void section_noexcept_audit() {
    std::cout << "\n=== noexcept audit ===\n";
    std::cout << "FileHandle  move-ctor noexcept? "
              << std::is_nothrow_move_constructible_v<FileHandle> << "\n";
    std::cout << "FileHandle  move-asgn noexcept? "
              << std::is_nothrow_move_assignable_v<FileHandle>    << "\n";
    std::cout << "OrderBook   move-ctor noexcept? "
              << std::is_nothrow_move_constructible_v<OrderBook>  << "\n";
    // ↑ all 1 → std::vector<T> will MOVE these during realloc instead of copy
}

int main() {
    section_exception_safety();
    section_noexcept();
    section_raii();
    section_filehandle();
    section_basic_vs_strong();
    section_noexcept_audit();
}
