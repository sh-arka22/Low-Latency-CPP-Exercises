# Chapter 2 — Essential C++ Techniques
> C++ High Performance (2nd Ed.) · Aligned to: `cpp-high-performance/03-essential-cpp-techniques/`

---

## BFS Layer 1 — What are the 6 topics and WHY do they matter?

| # | Topic | One-line "why it exists" | HFT relevance |
|---|-------|--------------------------|--------------|
| 1 | `auto` type deduction | Let the compiler write the correct type so you can't get it wrong | Avoids accidental copies of heavy types |
| 2 | Move semantics | Steal resources instead of copying them — O(1) instead of O(n) | Critical: STL containers must see `noexcept` move to use it during reallocation |
| 3 | Lambdas | Inline callable objects with captured state | Stateless lambdas in hot paths = zero overhead; avoids `std::function` penalty |
| 4 | `std::function` | Type-erased callable wrapper | Useful for callbacks, but has heap alloc + virtual dispatch overhead |
| 5 | Error handling (RAII + exception safety) | Guarantee invariants survive exceptions | `noexcept` on move/swap is a **performance contract**, not just style |
| 6 | `propagate_const` | Propagate `const` through pointer indirection | Pimpl idiom + const-safe APIs |

---

## BFS Layer 2 — Mental model for each topic

### 1. `auto` Type Deduction

The compiler already **knows** every expression's type. `auto` just writes it for you.

```
auto i  = 42;        // int        — copy
auto& r = x;         // int&       — reference (preserves const if x is const)
const auto& c = x;   // const int& — read-only alias, safe for temporaries
auto&& f = expr;     // forwarding ref — l or r depending on expr
decltype(auto) ret = expr;  // exact type including ref (needed for proxies)
```

**Key trap**: `auto` always strips top-level `const` and `&` from the initializer.
```cpp
const int ci = 10;
auto a = ci;   // int (NOT const int)
auto& ra = ci; // const int& (ref preserves constness)
```

**Range-for idiom**:
```cpp
for (const auto& price : order_book) { /* zero copy */ }
for (auto& price : order_book)       { /* zero copy, modifiable */ }
for (auto  price : order_book)       { /* COPIES each element — usually wrong */ }
```

---

### 2. Move Semantics

#### Value categories — every expression is one of:

```
   Expression
   ├── lvalue   → has identity, survives the expression  (int x; → x is lvalue)
   └── rvalue
       ├── prvalue  → no identity (temporary: 42, get_string())
       └── xvalue   → has identity BUT will expire (result of std::move(x))
```

#### The Rule of Five
If your class directly owns a resource (raw pointer, file handle, socket), define all 5:

```cpp
class Buffer {
    Buffer(const Buffer&);              // 1. copy ctor   — deep copy
    Buffer& operator=(const Buffer&);  // 2. copy assign — deep copy + self-assign guard
    ~Buffer();                         // 3. destructor  — delete
    Buffer(Buffer&&) noexcept;         // 4. move ctor   — steal + null source
    Buffer& operator=(Buffer&&) noexcept; // 5. move assign — steal + null source
};
```

`std::exchange(old, new)` is the idiom for move:
```cpp
Buffer(Buffer&& rhs) noexcept
    : size_(std::exchange(rhs.size_, 0))
    , ptr_ (std::exchange(rhs.ptr_,  nullptr)) {}
```

#### The Rule of Zero
If your members are RAII types (`std::vector`, `std::unique_ptr`, `std::string`):
→ define **none** of the 5 — compiler-generated versions do the right thing automatically.

#### noexcept on move — THE critical rule
```cpp
std::vector<Widget>::push_back(w);
// During reallocation:
//   if Widget::Widget(Widget&&) is noexcept → move (O(1)) ✓
//   if not marked noexcept               → COPY (O(n)) ← silent perf cliff!
```

Always mark move constructor + move assignment `noexcept`.

#### Common trap: moving non-resource scalars
```cpp
class Menu {
    int index_ = -1;           // scalar — move leaves OLD value in source!
    std::vector<std::string> items_; // resource — move nulls/clears this
};
// After move: items_ is empty, but index_ is still whatever it was
// Solution: std::swap(index_, rhs.index_) in move ctor
```

---

### 3. Lambda Expressions

**Mental model**: a lambda is a compiler-generated struct with `operator()`.

```
[capture](params) mutable -> return_type { body }
 ─────────         ──────   ───────          ────
 closure vars    function   allow modify   code
                 params     by-value caps
```

#### Capture table
| Syntax | Meaning |
|--------|---------|
| `[=]` | all by value (snapshot) |
| `[&]` | all by reference (live alias — careful of lifetime!) |
| `[x]` | x by value |
| `[&x]` | x by reference |
| `[x = std::move(y)]` | init-capture: move y into x at capture time |
| `[this]` | enclosing object by pointer |
| `[*this]` | enclosing object by copy (C++17 — safe for async) |

#### Performance hierarchy (hot path order):
1. **Stateless lambda** `[]{}` → direct call, fully inlineable, zero overhead
2. **Stateful lambda** `[x]{}` → closure struct on stack, still inlineable
3. **`std::function`** → type erasure, possible heap alloc, no inlining

Use `std::function` only for type-erased storage (callbacks stored in containers, GUI handlers). In hot paths, template the callable:
```cpp
template<typename F>
void on_fill(F&& handler) { handler(fill_data); }  // inlined, zero overhead
```

#### `mutable` lambdas
By default, by-value captures are `const` inside the lambda. `mutable` lifts this:
```cpp
auto counter = [c = 0]() mutable { return c++; };
```

#### Generic lambdas (C++14/20)
```cpp
auto add = [](auto a, auto b) { return a + b; };         // C++14
auto typed = []<typename T>(T a, T b) { return a + b; }; // C++20 (explicit template)
```

---

### 4. Error Handling — Exception Safety Guarantees

Four levels (strongest → weakest):

| Level | Guarantee | How |
|-------|-----------|-----|
| **No-throw** | Never throws | `noexcept`, swap, move ops, destructor |
| **Strong** | All-or-nothing: on exception, state is unchanged | copy-and-swap idiom |
| **Basic** | Valid state preserved, no resource leaks | careful ordering of operations |
| **None** | Undefined on exception | avoid |

#### Copy-and-Swap — the pattern for strong guarantee
```cpp
void Widget::update(const Number& x, const Number& y) {
    // Step 1: copies — can throw, but Widget is still untouched
    auto x_tmp = x;
    auto y_tmp = y;
    // Step 2: swap — noexcept, cannot fail
    std::swap(x_tmp, x_);
    std::swap(y_tmp, y_);
}
```

#### RAII — the foundation of all resource safety
```cpp
class MutexGuard {
    MutexGuard(Mutex& m) : m_(m) { m_.lock(); }
    ~MutexGuard()                { m_.unlock(); }  // runs even on exception
};
// equivalent to std::lock_guard / std::scoped_lock
```

#### When to use exceptions vs error codes in HFT:
- **Exceptions**: initialization, startup, configuration (not on hot path)
- **Error codes / `std::expected`**: per-message processing, order submission
- **Assertions**: invariant checks in debug builds (strip in release)

---

### 5. `propagate_const` — Const Through Pointer Indirection

```cpp
// WITHOUT propagate_const — const doesn't protect *ptr_:
const Foo f;
*f.ptr_ = 42;   // COMPILES — silent mutation through const object!

// WITH propagate_const — const propagates through operator*:
const Foo g;
*g.ptr_ = 42;   // DOES NOT COMPILE ✓
```

Used in **Pimpl idiom** to maintain const correctness:
```cpp
class OrderEngine {
    prop_const<OrderEngineImpl> impl_;  // const propagated into impl_
public:
    int count() const { return impl_->count; } // impl_ is const here ✓
    void place()      { impl_->count++; }      // impl_ is non-const ✓
};
```

---

## DFS Layer — Common Interview / HFT Questions per Topic

### Move Semantics
- "Why must move constructors be `noexcept`?" → STL fall-back to copy if not
- "What is `std::exchange` and why use it in move?" → atomic set-and-return old
- "What is an xvalue?" → result of `std::move()`; has identity but expires
- "What happens if you move from a moved-from object?" → valid but unspecified state

### Lambdas
- "Capture by `[&]` in async context — what goes wrong?" → dangling reference when scope ends
- "Why avoid `std::function` in hot paths?" → type erasure → heap alloc + virtual dispatch
- "Implement a recursive lambda" → init-capture `[self = std::ref(lambda)]`

### Error Handling
- "Implement strong guarantee for a `Container::push_back`" → allocate new, copy, swap
- "What does `noexcept` do for `std::vector`?" → enables move on reallocation
- "What is RAII?" → acquire in ctor, release in dtor; always runs, even on throw

---

## Connections to HFT / Low-Latency Systems

| Ch2 Technique | HFT Application |
|--------------|----------------|
| `noexcept` move | STL vector/deque used as order book storage — must move-reallocate in O(1) |
| RAII lock guards | Spinlock guards in market data publisher |
| Stateless lambdas | Per-message filters in the decode pipeline |
| Copy-and-swap | Atomic update of trading configuration without partial mutation |
| `auto&&` forwarding | Zero-copy message dispatch in the gateway layer |
| `propagate_const` | Pimpl'ed order engine — const read-path vs mutable write-path |
