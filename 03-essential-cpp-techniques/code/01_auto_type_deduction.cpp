/**
 * Chapter 2 — Essential C++ Techniques
 * Topic 1: auto Type Deduction
 *
 * FIRST PRINCIPLE:
 *   The compiler already knows the type of every expression. `auto` tells
 *   it to write that type down for you — no new semantics, just convenience.
 *
 * KEY RULES:
 *   1. `auto`        → copy (strips refs and top-level const)
 *   2. `auto&`       → lvalue reference (preserves const)
 *   3. `const auto&` → const lvalue ref (read-only alias, safe for temporaries)
 *   4. `auto&&`      → forwarding reference (collapses to l or r depending on init)
 *   5. `decltype(auto)` → exact type including ref/const (needed for return proxies)
 *
 * HFT NOTE:
 *   In hot paths, auto avoids accidental copies of heavy types (e.g., large PODs,
 *   strings). Use `const auto&` for range-for loops over containers to guarantee
 *   zero copies.
 */

#include <iostream>
#include <string>
#include <vector>
#include <type_traits>

// ─── SECTION 1: basic deduction ──────────────────────────────────────────────

void section_basic() {
    auto i  = 42;         // int
    auto d  = 3.14;       // double
    auto s  = std::string{"hello"};  // std::string (copy from rvalue)
    auto v  = std::vector<int>{1, 2, 3}; // vector<int>

    // auto strips const and reference from the initializer expression
    const int ci = 10;
    auto a = ci;    // a is int (not const int) — top-level const stripped
    auto& ra = ci;  // ra is const int& — ref preserves the constness of ci

    static_assert(std::is_same_v<decltype(a),  int>);
    static_assert(std::is_same_v<decltype(ra), const int&>);
    (void)i; (void)d; (void)s; (void)v;
}

// ─── SECTION 2: auto on function return types ─────────────────────────────────

class Foo {
    int m_ = 42;
public:
    // auto return: deduced as int (value copy)
    auto val()  const { return m_; }

    // auto& return: deduced as const int& (no copy)
    auto& cref() const { return m_; }

    // auto& return: deduced as int& (mutable ref)
    auto& mref()       { return m_; }
};

void section_return_types() {
    auto foo = Foo{};

    // cref → const int& (cannot modify through it)
    auto& c = foo.cref();
    static_assert(std::is_const_v<std::remove_reference_t<decltype(c)>>);

    // mref → int& (can modify)
    auto& m = foo.mref();
    static_assert(!std::is_const_v<std::remove_reference_t<decltype(m)>>);

    (void)c; (void)m;
}

// ─── SECTION 3: forwarding reference (universal reference) ────────────────────

template<typename T>
void inspect(T&& t) {
    if constexpr (std::is_lvalue_reference_v<T&&>) {
        std::cout << "lvalue ref\n";
    } else {
        std::cout << "rvalue ref\n";
    }
    (void)t;
}

void section_forwarding_ref() {
    int x = 5;
    inspect(x);          // T=int&  → lvalue ref
    inspect(5);          // T=int   → rvalue ref
    inspect(std::move(x)); // T=int → rvalue ref
}

// ─── SECTION 4: decltype(auto) for exact return type ─────────────────────────

// Without decltype(auto): auto always strips ref → returns value copy
auto get_val(std::vector<int>& v, int i) { return v[i]; }       // returns int (copy)
decltype(auto) get_ref(std::vector<int>& v, int i) { return v[i]; } // returns int&

void section_decltype_auto() {
    auto v = std::vector<int>{10, 20, 30};
    auto  copy = get_val(v, 0);  // int  — copy
    decltype(auto) ref = get_ref(v, 0); // int& — reference

    ref = 99;  // modifies v[0] directly
    std::cout << "v[0] after ref modify: " << v[0] << "\n"; // 99
    (void)copy;
}

// ─── SECTION 5: range-for best practices ─────────────────────────────────────

void section_range_for() {
    auto prices = std::vector<double>{99.5, 100.0, 100.5};

    // BAD: `auto p` makes a copy of each double (harmless for double,
    //       catastrophic for large objects)
    for (auto p : prices) { (void)p; }

    // GOOD: const auto& → zero copy, read-only alias
    for (const auto& p : prices) { (void)p; }

    // GOOD: auto& → zero copy, mutable (use when you need to modify)
    for (auto& p : prices) { p *= 1.01; }
}

int main() {
    section_basic();
    section_return_types();
    section_forwarding_ref();
    section_decltype_auto();
    section_range_for();
    std::cout << "All auto sections ran OK\n";
}
