# Chapter 2 — TODO Task List
> C++ High Performance (2nd Ed.) | Essential C++ Techniques
> Folder: `cpp-high-performance/03-essential-cpp-techniques/`
> Linked code: `code/01_auto_type_deduction.cpp` through `code/05_propagate_const.cpp`

---

## ✅ HOW TO USE THIS FILE
- Work topic by topic, level by level (BFS → DFS)
- Mark `[ ]` → `[x]` when done
- Each Level 3 task is an interview/production challenge

---

## TOPIC 1 — `auto` Type Deduction

### Level 1 — Understand (Read + trace)
- [ ] Read `code/01_auto_type_deduction.cpp` section by section
- [ ] Trace `section_basic()`: what type does each `auto` deduce?
- [ ] Answer: what does `auto a = ci` give when `ci` is `const int`?
- [ ] Answer: what is the difference between `auto&` and `const auto&`?

### Level 2 — Apply (Write code)
- [ ] Write a function `max_price(const std::vector<double>& v)` using `auto&` range-for
- [ ] Write a class with `auto val()`, `auto& cref()`, `auto& mref()` return types
- [ ] Verify with `static_assert` + `std::is_same_v` that types are as expected
- [ ] Experiment: change `auto` to `decltype(auto)` in `get_ref()` — observe the difference

### Level 3 — HFT Challenge
- [ ] Write a `PriceCache` class using `decltype(auto)` return type that acts as a proxy
      into an internal `std::vector<double>` — const and non-const overloads must work
- [ ] Benchmark: range-for with `auto p` vs `const auto& p` on 1M doubles — measure ns diff

---

## TOPIC 2 — Move Semantics

### Level 1 — Understand
- [ ] Read `code/02_move_semantics.cpp` top to bottom
- [ ] Draw the value category tree: lvalue / prvalue / xvalue
- [ ] Trace `demo_rule_of_five()`: which of the 5 operations is called at each line?
- [ ] Answer: why must move constructor be `noexcept` for `std::vector`?
- [ ] Answer: what does `std::exchange(old, 0)` do — in one sentence?

### Level 2 — Apply
- [ ] Implement `class RingBuffer` with the full Rule of Five (no `std::vector` internally — use raw `T* ptr_`)
- [ ] Verify: push `RingBuffer` into `std::vector`, check that realloc uses move (not copy)
      Hint: add print to move ctor; run with `reserve()` then without
- [ ] Fix the `Menu` class pitfall: make the moved-from `Menu` print `"(none)"` safely
- [ ] Add ref-qualifiers `&` / `&&` to a `Price` struct's `value()` method

### Level 3 — HFT Challenge
- [ ] Implement `class OrderMessage` (Pimpl'd) where all operations are `noexcept`
- [ ] Implement a `move-only` `UniqueSocket` class that wraps a file descriptor (int)
      — no copy, move transfers ownership, destructor calls `close(fd_)`
- [ ] Write a `std::sort` benchmark on `std::vector<OrderMessage>` with and without
      `noexcept` on the move ctor — measure the difference

---

## TOPIC 3 — Lambdas

### Level 1 — Understand
- [ ] Read `code/03_lambdas.cpp` all sections
- [ ] Answer: what is the equivalent struct the compiler generates for `[x](int v){ return v > x; }`?
- [ ] Trace `section_capture()`: what count do the by-value and by-ref lambdas return and why?
- [ ] Answer: why does `[&]` in an async context cause undefined behaviour?

### Level 2 — Apply
- [ ] Use `std::sort` with a lambda to sort `std::vector<Order>` by price ascending, then by quantity descending
- [ ] Write a `mutable` lambda that accumulates a running P&L total
- [ ] Write a generic lambda that prints any container using range-for
- [ ] Use init-capture `[buf = std::move(buffer)]` to transfer ownership into a lambda

### Level 3 — HFT Challenge
- [ ] Implement a `Pipeline<F...>` variadic template that chains stateless lambdas:
      `pipeline(decode, validate, route)(raw_packet)` — zero virtual dispatch
- [ ] Implement a recursive lambda that flattens a nested `std::vector<std::vector<int>>`
      using init-capture: `[&self = *this]` pattern
- [ ] Profile: `std::function<void(int)>` callback vs template-parameter callback
      in a tight loop (1M iterations) — report the overhead

---

## TOPIC 4 — `std::function`

### Level 1 — Understand
- [ ] Read `section_std_function()` in `code/03_lambdas.cpp`
- [ ] Answer: why can `std::function` store a lambda, a functor, AND a function pointer?
- [ ] Answer: what is "type erasure" — explain in terms of virtual dispatch
- [ ] Answer: when does `std::function` allocate on the heap?

### Level 2 — Apply
- [ ] Implement a `CallbackManager` that stores `std::function<void(int)>` handlers by event ID
- [ ] Replace `std::function` with a template parameter — verify it inlines in `perf`/assembly

### Level 3 — HFT Challenge
- [ ] Implement a `FunctionRef<R(Args...)>` (non-owning, non-allocating callable wrapper)
      that avoids the heap allocation of `std::function` — similar to `std::function_ref` (C++26)

---

## TOPIC 5 — Error Handling (RAII + Exception Safety)   ✅ COMPLETE

### Level 1 — Understand
- [x] Read `code/04_error_handling.cpp`
- [x] List the 4 exception safety levels from memory
      → **no-throw** (`noexcept`, terminates if violated) >
        **strong** (all-or-nothing, copy-and-swap) >
        **basic** (valid state, no leaks, but possibly mutated) >
        **none** (UB).
- [x] Trace `Widget::update()`: why does copy-and-swap give strong guarantee?
      → all throwing work happens on locals (`x_tmp`, `y_tmp`); the only
        operations that touch `*this` are `std::swap` calls, which are
        `noexcept` on trivial types. If either copy throws, `*this` is
        bit-for-bit unchanged.
- [x] Answer: what happens if you call a `noexcept` function that throws internally?
      → `std::terminate()` is invoked immediately (stack-unwinding stops at
        the noexcept boundary). Used intentionally to enforce invariants.

### Level 2 — Apply
- [x] Rewrite `WidgetUnsafe::update_bad()` to have strong exception guarantee
      → `WidgetUnsafe::update_strong()` in `code/04_error_handling.cpp`
- [x] Implement `class FileHandle` using RAII: open in ctor, `fclose` in dtor
      → `FileHandle` in `code/04_error_handling.cpp` (Section 4), move-only,
        `noexcept` move ops, throws on `fopen` failure.
- [x] ~~Mark all your move ops from Topic 2 as `noexcept`~~ — Topic 2 not done;
      audited the move ops *in this file* instead (`section_noexcept_audit`).
- [x] Write a function that gives basic guarantee but NOT strong — explain why
      → `append_all_basic()` vs `append_all_strong()` (Section 5). Basic version
        may partially append if a copy throws mid-loop — vector is still valid
        but cannot be rolled back to its pre-call state.

### Level 3 — HFT Challenge
- [x] Implement `OrderBook::add_order()` with the strong exception guarantee
      → `code/04_order_book_strong.cpp`. Copies both `bids_` and `asks_` price
        maps into locals, mutates the copies, commits via two noexcept
        `std::map::swap`s. Verified via `add_order_with_injected_throw()` test
        path that book is bit-for-bit unchanged when a throw fires.
- [x] Implement a `ScopedTimer` RAII class that measures nanosecond latency of a
      code block and logs the result to a circular buffer on destruction
      → `ScopedTimer` + `CircularLog<N>` in `04_order_book_strong.cpp`.
        Move-only, `noexcept` everywhere, lock-free single-producer push via
        relaxed atomic head index, fixed-capacity (no alloc in measured path).

---

## TOPIC 6 — `propagate_const`

### Level 1 — Understand
- [ ] Read `code/05_propagate_const.cpp`
- [ ] Answer: why does `const Foo f; *f.ptr_ = 42;` compile?
- [ ] Answer: what does `prop_const<T>::operator->() const` return vs non-const version?

### Level 2 — Apply
- [ ] Wrap the `OrderEngine` Pimpl pointer with `prop_const<Impl>`
- [ ] Verify that a `const` `OrderEngine` cannot call `place_order()` (should not compile)
- [ ] Use `std::experimental::propagate_const` if your compiler supports it

### Level 3 — HFT Challenge
- [ ] Implement a full Pimpl `MarketDataFeed` class using `prop_const`:
      - `const` methods: `last_price()`, `bid()`, `ask()`
      - non-`const` methods: `connect()`, `subscribe(symbol)`
      - Impl hidden in `.cpp` to reduce compile-time coupling

---

## END-OF-CHAPTER INTEGRATION TASKS

- [ ] Build all 5 code files cleanly: `g++ -std=c++20 -Wall -Wextra code/*.cpp`
- [ ] Write a single `order_message.cpp` that uses ALL six techniques together:
      - `auto` for type-safe member access
      - Move semantics for zero-copy message passing
      - Lambda for per-field validation
      - RAII for socket/file lifecycle
      - `propagate_const` in Pimpl
- [ ] Push code to `gh:sh-arka22/Low-Latency-CPP-Exercises` with commit message format:
      `ch02: [topic] brief description`
- [ ] Write 3 interview Q&A pairs per topic in `EXERCISES.md`
