# Mini Quote Engine — L2 capstone

A tick-processing pipeline that ingests market data and maintains a
top-of-book quote. You implement it **twice**:

  1. `naive.cpp` — the obvious version. Violates every rule from L2.1, L2.4, L2.5.
  2. `fast.cpp`  — the same logic, applying every rule.

Then benchmark both and prove the speedup is real.

──────────────────────────────────────────────────────────────────────
## What the engine does

Input: an array of N = 10,000,000 synthetic ticks. Each tick has a
price, qty, side (buy/sell), and venue (LSE/NYSE/NASDAQ/BATS).

For each tick:
  1. Validate it (qty>0, price in sane range, venue active).
  2. Compute a venue-specific adjusted price (apply per-venue fee).
  3. If the tick is valid and improves the current top-of-book for that
     venue, update top-of-book.
  4. XOR the venue's current best bid+ask timestamps into a running
     checksum (this prevents the optimizer from deleting everything).

Output: the running checksum, plus cycles-per-tick.

──────────────────────────────────────────────────────────────────────
## The pipeline (4 stages)

   Ticks[N]  ──►  Validate  ──►  VenueDispatch  ──►  UpdateBook  ──►  checksum
   (stack)        (L2.4)         (L2.5 CRTP)        (L2.1 + RVO)

──────────────────────────────────────────────────────────────────────
## Phase 1 — naive.cpp (must violate ALL these rules)

Write the obvious code. Specifically:

  L2.1 violations:
    [ ] `Tick` struct: members ordered worst-case (char before double,
        short between ints). Verify with sizeof.
    [ ] `Quote` struct: NO alignas(64). Two thread-shared Quotes adjacent.
    [ ] Allocate the tick array with `new[]` instead of stack/static.

  L2.4 violations:
    [ ] Fee per venue: implement as a `switch` statement.
    [ ] Validation chain: order checks worst (expensive first).
    [ ] No [[likely]]/[[unlikely]] hints anywhere.
    [ ] Side-specific logic: runtime `if (side == BUY) ... else ...`.

  L2.5 violations:
    [ ] Use `virtual` dispatch for venue handlers. Base class + 4 derived.
    [ ] Use `std::function<void(const Tick&)>` somewhere on the hot path.
    [ ] Return Quote from helper by copying via a named local (NRVO ok,
        but introduce a branch that returns one of TWO named locals).
    [ ] One helper written recursively (e.g. compute total fee across
        N levels of a nested venue tree, or a recursive validation
        chain — your call).

  Run the benchmark on this version and record cycles-per-tick.

──────────────────────────────────────────────────────────────────────
## Phase 2 — fast.cpp (must apply ALL these rules)

Same exact input → same exact checksum → much faster.

  L2.1 fixes:
    [ ] Reorder `Tick` members largest→smallest. sizeof must be minimal.
    [ ] sizeof(Tick) MUST fit in 32 bytes (so 2 ticks per cache line).
    [ ] `Quote` aligned to 64 bytes via alignas(64) — no false sharing.
    [ ] Tick array on the stack (or static), NOT heap.

  L2.4 fixes:
    [ ] Fee lookup: `constexpr` array indexed by VenueID.
    [ ] Validation: cheapest/most-likely-false check first.
    [ ] [[likely]] on the validate-passes branch (97%+ pass rate).
    [ ] Side dispatch: `if constexpr` inside a `template<Side S>` function.

  L2.5 fixes:
    [ ] CRTP venue handler hierarchy. ZERO `virtual` keyword in fast.cpp.
    [ ] All hot-path helpers marked `[[gnu::always_inline]] inline`.
    [ ] NO `std::function` in fast.cpp. Use function templates instead.
    [ ] Quote returned via prvalue (`return Quote{...};`) — guaranteed RVO.
    [ ] All loops iterative. No recursion (constexpr recursion is fine
        if you have any compile-time constants).

──────────────────────────────────────────────────────────────────────
## Phase 3 — Benchmark and report

  $ make
  $ ./bench

  Expected output format:
    NAIVE:   cycles/tick = 142.7   total = 1.42 s   checksum = 0xab12...
    FAST:    cycles/tick =  28.3   total = 0.28 s   checksum = 0xab12...
    SPEEDUP: 5.04x

  Acceptance criteria:
    [ ] Checksums MUST match exactly. If they differ, you've broken
        the logic during optimization. Fix it.
    [ ] FAST must be at least 4x faster than NAIVE.
    [ ] sizeof(fast::Tick) <= 32.
    [ ] sizeof(fast::Quote) == 64 (cache-line aligned).

  Use the included bench/main.cpp — don't roll your own timing.

──────────────────────────────────────────────────────────────────────
## Writeup (the part that locks the learning in)

After the numbers match, write a 1-page WRITEUP.md answering:

  1. Per L2 node, list which specific change gave the biggest speedup.
     (Hint: it's usually one of: CRTP-for-virtual, lookup-for-switch,
      or removing std::function.)

  2. Inspect the asm for one of fast.cpp's hot helpers. Show that
     it compiles to NO `call` instruction (it was inlined).
     Use: `g++ -O2 -S fast.cpp -o fast.s` and grep for your helper.

  3. What happened to sizeof(Tick)? Show before / after with the
     padding bytes annotated.

  4. If you removed alignas(64) from Quote, would the single-threaded
     benchmark show any difference? Why or why not?
     (Answer: no — alignas(64) is a multi-threaded false-sharing fix.
      State this clearly to confirm you understand WHY each rule exists.)

──────────────────────────────────────────────────────────────────────
## Build

  $ make           # builds bench
  $ make clean
  $ ./bench

Compiler: g++ or clang++ with -O2 -std=c++20.
No external deps. Pure standard library.

──────────────────────────────────────────────────────────────────────
## File map

  README.md            ← this file
  Makefile             ← single command build
  include/
    common.h           ← shared types (VenueID, Side), rdtsc, constants
  src/
    naive.h            ← naive types + interface (you fill TODOs)
    naive.cpp          ← naive implementation (you write)
    fast.h             ← fast types + interface (you fill TODOs)
    fast.cpp           ← fast implementation (you write)
  bench/
    gen_data.h/cpp     ← deterministic tick generator (provided)
    main.cpp           ← benchmark harness (provided — do NOT modify)
# Low-Latency-CPP-Exercises
