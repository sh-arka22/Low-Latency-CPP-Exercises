# Chapter 1 / Repo Ch.2 — TODO

> Hierarchical study plan. Tick boxes left-to-right; don't skip levels.
> Each leaf = one runnable file in `code/`. Stop after each leaf and **re-derive the rule** in your own words before moving on.

---

## Level 1 — Foundations (must finish before Chapter 2 of the book)

- [ ] **L1.1** Read `NOTES.md` end-to-end. No skipping.
- [ ] **L1.2** Clone & build the official Packt code locally:
      ```
      git clone https://github.com/PacktPublishing/Cpp-High-Performance-Second-Edition
      cd Cpp-High-Performance-Second-Edition/Chapter01
      cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
      ./build/Chapter01-A_Brief_Introduction_to_C++
      ```
- [ ] **L1.3** Verify `gtest` & `gbench` are installed (see `../01-getting-started/TODO.md`).
- [ ] **L1.4** Answer all 8 self-check questions in `NOTES.md` from memory.

---

## Level 2 — Hands-on per section (one commit per leaf)

### 2.1 Zero-cost abstractions
- [ ] **L2.1.a** Implement `code/abstractions.cpp` — C version + C++ version of `num_hamlet`.
- [ ] **L2.1.b** Compile both to assembly with `-O2 -S`. Diff. Note how many extra instructions the C++ version emits (target: 0–2).
- [ ] **L2.1.c** Replace `std::forward_list` with `std::vector`. Re-diff assembly. Hypothesise the cache-locality difference.

### 2.2 Memory layout — stack vs heap, contiguous vs indirected
- [ ] **L2.2.a** Implement `code/heap_allocations.cpp` — stack-only `Car` + `vector<Car>` with `reserve`.
- [ ] **L2.2.b** Add a microbenchmark (gbench) iterating `std::vector<Car>` (1M elements) vs `std::vector<std::unique_ptr<Car>>`. Record cycles/element.
- [ ] **L2.2.c** Repeat L2.2.b with `Car` padded to 4 KiB (one per page). Observe the catastrophe.

### 2.3 Value semantics
- [ ] **L2.3.a** Implement `code/value_semantics.cpp` — `Bagel(std::set<std::string>)`.
- [ ] **L2.3.b** Add a test that proves mutating the original `set` does **not** modify the `Bagel`.
- [ ] **L2.3.c** Replace by-value with by-const-ref. Show that the test still passes — then discuss why pass-by-value is still preferred when the callee will store the parameter (the "sink" idiom).

### 2.4 Const correctness
- [ ] **L2.4.a** Implement `code/const_correctness.cpp` — `Person` and `Team`, with `nonmutating_func` and `mutating_func`.
- [ ] **L2.4.b** Uncomment the `set_age(20)` line in `nonmutating_func`. Confirm the compiler diagnoses it. Read the error.
- [ ] **L2.4.c** Add `const_cast<Person&>(team.leader()).set_age(20)` — make it compile. *Why is this a footgun?*

### 2.5 References vs pointers
- [ ] **L2.5.a** Implement `code/references.cpp` — `get_volume1(const Sphere&)` and `get_volume2(const Sphere*)`.
- [ ] **L2.5.b** Try `get_volume1(nullptr)`. Read the compiler diagnostic.
- [ ] **L2.5.c** Replace `const Sphere*` with `std::optional<std::reference_wrapper<const Sphere>>`. Compare clarity.

### 2.6 Strict interfaces (stub — book code repo doesn't ship this)
- [ ] **L2.6.a** Create `code/strict_interfaces.cpp` — `Engine`, `YamahaEngine`, `Boat` holding `shared_ptr<Engine>`.
- [ ] **L2.6.b** Write the buggy test: copy a `Boat`, mutate engine on copy, assert original is unchanged → it **fails** because the shared_ptr aliases.
- [ ] **L2.6.c** Add `Boat(const Boat&) = delete` + `operator=(const Boat&) = delete`. Try to copy. Read the diagnostic.
- [ ] **L2.6.d** Provide an explicit `Boat clone() const` that deep-copies the engine. Re-run the test from L2.6.b — assert original is unchanged.

### 2.7 Copy-and-swap (stub)
- [ ] **L2.7.a** Create `code/copy_and_swap.cpp` — `OakTree` with two vectors.
- [ ] **L2.7.b** Write the *naïve* `operator=` (field-by-field). Add a test that throws `bad_alloc` mid-assignment (use a custom allocator); observe `OakTree` is wrecked.
- [ ] **L2.7.c** Rewrite `operator=` with copy-and-swap. Re-run the throwing test; observe `OakTree` is untouched.

### 2.8 RAII / lock_guard (stub)
- [ ] **L2.8.a** Create `code/raii_lock_guard.cpp` — a function with early return, exception throw, and fall-through; all three must release the mutex.
- [ ] **L2.8.b** Add a destructor counter (RAII guard with side-effect logging). Confirm destruction order across nested scopes.

### 2.9 Exceptions vs error codes (HFT extra)
- [ ] **L2.9.a** Write two `parse_int` functions: one returning `int` and throwing on bad input; one returning `std::expected<int, ParseError>` (or `std::optional`).
- [ ] **L2.9.b** Benchmark both for 1M valid inputs (non-throwing path); confirm the exception version has **zero** overhead.
- [ ] **L2.9.c** Benchmark with 1% bad inputs. Quantify the throwing overhead.

---

## Level 3 — Synthesis exercises (interview-grade)

- [ ] **L3.1** Without looking at the book, design an `OrderBook` row that follows all of Chapter 1's principles: value-semantic, const-correct, non-copyable (move-only), RAII-managed memory.
- [ ] **L3.2** Show a code review checklist (8 bullets max) you'd apply to any pull request in a low-latency C++ codebase, derived from Chapter 1.
- [ ] **L3.3** Explain to an interviewer in <90 seconds *why* `std::vector<Order>` is preferable to `std::vector<std::shared_ptr<Order>>` on the hot path. Include cache lines, allocations, and atomic ops.

---

## Level 4 — Memory persistence

- [ ] **L4.1** Update the project-level memory file with three things you didn't already know.
- [ ] **L4.2** Cross-link this chapter's NOTES to the existing memory entries on `SpinBarrier` (RAII connection) and `Radix Tree` (cache-locality connection).

---

## Definition of done

You're done with Chapter 1 when:
1. Every L2 leaf compiles and passes its test.
2. You can answer all 8 self-check questions in `NOTES.md` *without* looking back.
3. The L3 synthesis exercises are committed to this folder as `EXERCISES.md`.
4. Memory updated (L4).
