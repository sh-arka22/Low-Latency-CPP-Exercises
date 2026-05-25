/**
 * Chapter 2 — Essential C++ Techniques
 * Topic 3: Lambda Expressions — Anonymous Closures
 *
 * ── FIRST PRINCIPLES ──────────────────────────────────────────────────────────
 *
 * A lambda IS a compiler-generated struct with operator() defined.
 * The compiler does this under the hood:
 *
 *   auto is_even = [](int n){ return n % 2 == 0; };
 *
 *   // What the compiler actually generates:
 *   struct __lambda_1 {
 *       bool operator()(int n) const { return n % 2 == 0; }
 *   };
 *   auto is_even = __lambda_1{};
 *
 * CAPTURE MODES:
 *   [=]        — capture all by value (snapshot of current scope)
 *   [&]        — capture all by reference (live alias — danger if lambda outlives scope)
 *   [x]        — capture x by value
 *   [&x]       — capture x by reference
 *   [x, &y]    — mixed
 *   [this]     — capture enclosing object by pointer
 *   [*this]    — capture enclosing object by copy (C++17, safe for async)
 *   [x = expr] — init capture: run expr at capture time, store as x
 *
 * HFT NOTE:
 *   Prefer stateless lambdas (no captures) in hot paths — they compile to a
 *   direct function call (no indirection). std::function adds virtual dispatch
 *   overhead; avoid in latency-critical code.
 */

#include <algorithm>
#include <functional>
#include <iostream>
#include <list>
#include <vector>

// ─── SECTION 1: Basic lambda syntax ──────────────────────────────────────────

void section_basic() {
    std::cout << "\n=== Basic Lambda ===\n";

    // Stateless lambda (no capture)
    auto is_above = [](int v) { return v > 3; };

    auto vals = std::vector<int>{1, 3, 2, 5, 4};
    auto count = std::count_if(vals.begin(), vals.end(), is_above);
    std::cout << "Count above 3: " << count << "\n"; // 2

    // Inline lambda
    auto count2 = std::count_if(vals.begin(), vals.end(),
                                [](int v){ return v > 3; });
    (void)count2;
}

// ─── SECTION 2: Capture by value vs. reference ───────────────────────────────

void section_capture() {
    std::cout << "\n=== Capture ===\n";

    auto vals = std::vector<int>{1, 2, 3, 4, 5, 6};

    // Capture by VALUE: snapshot of x at creation time
    {
        auto x = 3;
        auto is_above = [x](int v) { return v > x; }; // x captured = 3
        x = 100; // does NOT affect is_above
        auto c = std::count_if(vals.begin(), vals.end(), is_above);
        std::cout << "By value (x=3 snap): " << c << "\n"; // 3
    }

    // Capture by REFERENCE: live alias
    {
        auto x = 3;
        auto is_above = [&x](int v) { return v > x; }; // x captured by ref
        x = 4; // DOES affect is_above
        auto c = std::count_if(vals.begin(), vals.end(), is_above);
        std::cout << "By ref (x=4 live):   " << c << "\n"; // 2
    }

    // Init capture: evaluate expression at capture time, name it
    {
        auto c = std::list<int>{4, 2};
        auto func = [my_list = std::move(c)]() {
            for (auto v : my_list) std::cout << v << " ";
            std::cout << "\n";
        };
        // c is now empty; my_list owns the data
        func();
    }
}

// ─── SECTION 3: mutable lambdas ──────────────────────────────────────────────

void section_mutable() {
    std::cout << "\n=== Mutable Lambda (stateful closure) ===\n";

    // By default operator() is const → captured values can't be modified
    // `mutable` lifts that restriction for by-value captures
    auto counter = [c = 0]() mutable {
        std::cout << "count=" << c++ << "\n";
    };
    counter(); // 0
    counter(); // 1
    counter(); // 2

    // The counter variable itself is untouched:
    // (c is inside the closure struct, not a reference to any external variable)
}

// ─── SECTION 4: Generic lambdas (C++14/20) ───────────────────────────────────

void section_generic() {
    std::cout << "\n=== Generic Lambda ===\n";

    // auto parameter → template operator()
    auto add = [](auto a, auto b) { return a + b; };
    std::cout << add(1, 2) << "\n";       // int: 3
    std::cout << add(1.5, 2.5) << "\n";  // double: 4.0

    // C++20 explicit template parameter in lambda
    auto typed = []<typename T>(T a, T b) -> T { return a + b; };
    std::cout << typed(10, 20) << "\n";  // 30
    // typed(1, 2.0); // would fail — T must be consistent
}

// ─── SECTION 5: Lambda types (C++20) ─────────────────────────────────────────

void section_lambda_types() {
    std::cout << "\n=== Lambda Types (C++20) ===\n";

    // Stateless lambdas (no capture) are default-constructible and copyable in C++20
    auto x = []{};
    auto y = x;         // copy
    decltype(y) z;      // default-construct same type

    static_assert(std::is_same_v<decltype(x), decltype(y)>);
    static_assert(std::is_same_v<decltype(x), decltype(z)>);
    (void)z;
    std::cout << "Same type? yes (stateless lambda)\n";
}

// ─── SECTION 6: std::function — type-erased callable ─────────────────────────
//     (separate topic in book, included here for completeness)

class Button {
public:
    explicit Button(std::function<void()> on_click)
        : handler_(std::move(on_click)) {}

    void click() const { handler_(); }

private:
    std::function<void()> handler_;
};

void section_std_function() {
    std::cout << "\n=== std::function ===\n";

    // std::function can hold any callable (lambda, functor, function ptr)
    auto beep_count = 0;
    auto beep = Button([&beep_count]() mutable {
        std::cout << "Beep #" << ++beep_count << "\n";
    });
    auto bop  = Button([] { std::cout << "Bop\n"; });

    beep.click(); // Beep #1
    beep.click(); // Beep #2
    bop.click();  // Bop

    // PERFORMANCE CAUTION:
    // std::function uses type erasure → potential heap allocation + virtual dispatch
    // In HFT hot paths: use template parameters (monomorphic) instead
    //   template<typename F> void register_handler(F&& f);
    // This keeps the call direct and allows inlining.
}

int main() {
    section_basic();
    section_capture();
    section_mutable();
    section_generic();
    section_lambda_types();
    section_std_function();
}
