# Chapter 4 — TODO Task List
> C++ High Performance (2nd Ed.) | Analyzing and Measuring Performance
> Folder: `cpp-high-performance/04-analyzing-and-measuring-performance/`
> Linked code: `code/01_big_o.cpp`, `code/02_amortized.cpp`, `code/03_profiling.cpp`, `code/04_cache_effects.cpp`

---

## ✅ HOW TO USE THIS FILE
- Work topic by topic, level by level (BFS → DFS)
- Mark `[ ]` → `[x]` when done
- Level 1 = understand the idea. Level 2 = implement it. Level 3 = HFT-grade challenge.

---

## TOPIC 1 — Asymptotic Complexity & Big O

### Level 1 — Understand (Read + trace)
- [ ] Open `code/01_big_o.cpp` and read all sections top to bottom
- [ ] Trace `section_linear_search()`: what is the Big O and *why*? Write it in one sentence
- [ ] Trace `section_binary_search()`: why does halving give O(log n)?
      Hint: if n=64, how many halvings until you reach 1 element?
- [ ] Derive Big O for `section_insertion_sort()` manually: count swap operations as a sum, simplify
- [ ] Answer: two algorithms are O(n). Algorithm A takes 2ms for n=1000; B takes 100ms for n=1000.
      Which wins for n=10,000,000? Is Big O still the right tool to choose? Why or why not?

### Level 2 — Apply (Write code)
- [ ] Write `nested_loop_complexity()`: two nested loops over n elements — derive Big O and verify by timing
- [ ] Write `log_n_demo()`: count how many times you must halve n before reaching 1 — plot n vs count
- [ ] Given `f(n) = 5n³ + 10n² + 1000n + 42`, write the Big O — show your steps
- [ ] Look up `std::sort`'s complexity guarantee in cppreference — write it as Big O with an explanation

### Level 3 — HFT Challenge
- [ ] Implement `order_book_lookup()`: a price-level lookup using `std::map<int,int>` (O(log n))
      vs `std::unordered_map<int,int>` (O(1) amortized)
      — benchmark both with n = 100, 10,000, 1,000,000 price levels using `<chrono>`
      — at what n does unordered_map start winning? Plot or table the results
- [ ] Write a function that is O(n log n) on paper, then verify experimentally by timing it at
      n = 1k, 10k, 100k, 1M and checking if the ratio of times matches n log n growth

---

## TOPIC 2 — Amortized Complexity

### Level 1 — Understand
- [ ] Open `code/02_amortized.cpp` and read `section_vector_growth()`
- [ ] Answer: `push_back` is O(n) on a resize. How can the *amortized* cost still be O(1)?
      (Hint: doubling strategy — write out total work for n=8 pushes from empty vector)
- [ ] Answer: what is the difference between amortized O(1) and average O(1)?
- [ ] What does `reserve()` do? When should you call it and when is it dangerous *not* to?
- [ ] What does `shrink_to_fit()` do? Is it guaranteed to work?

### Level 2 — Apply
- [ ] Write `instrument_push_back()`: track how many times reallocation fires for 1000 push_backs
      on a default-constructed vector — count reallocs by watching `capacity()` change
- [ ] Verify that `reserve(1000)` before 1000 push_backs causes exactly 0 reallocations
- [ ] Write a custom `GrowthTracker<T>` that wraps a vector and logs the capacity each time it changes
- [ ] Use `emplace_back` instead of `push_back` with a non-trivial struct — verify with `static_assert`
      that no extra copy is made (hint: make the copy constructor `= delete`)

### Level 3 — HFT Challenge
- [ ] Implement a `PreallocatedQueue<T, N>` — fixed-capacity ring buffer, no dynamic allocation,
      all ops guaranteed O(1) worst-case (not just amortized)
      — use `std::array<T, N>` internally with head/tail indices
      — demonstrate that it can push/pop in a tight loop with **zero** allocations
- [ ] Benchmark `PreallocatedQueue<int,1024>` vs `std::deque<int>` for 1M push+pop ops
      — measure ns/op and report allocation count (use `valgrind --tool=massif` or override `::operator new`)

---

## TOPIC 3 — Performance Properties & Testing Best Practices

### Level 1 — Understand
- [ ] Define in your own words: latency, throughput, CPU-bound, I/O-bound, memory-bound
- [ ] Why is the *median* more useful than the *mean* for latency measurements?
- [ ] What is the Pareto Principle? How does it guide where you spend optimization effort?
- [ ] Answer: an image converter is CPU-bound. A database is I/O-bound. What do you optimize first in each?

### Level 2 — Apply
- [ ] Write `measure_latency()`: run a function 10,000 times, collect all durations in a `vector`,
      then compute min, max, mean, median, p99 (99th percentile)
- [ ] Write a test that generates 1M, 10M, 100M integers and sorts them — plot the time vs n on paper
      to confirm it's O(n log n)
- [ ] Find the "hot function" in the following scenario: you have 4 functions;
      you run them 10,000 times and collect timings — which one to optimize first? (write the logic)

### Level 3 — HFT Challenge
- [ ] Build a `LatencyHistogram<N>` class:
      - Records latency samples into N fixed-width buckets (e.g. 0-9ns, 10-19ns, …)
      - No dynamic allocation (`std::array<uint64_t, N>` internally)
      - `report()` method prints: min, max, p50, p95, p99, p999 latencies
      - Use `__rdtsc()` (x86 TSC counter) for sub-nanosecond timing
      - Record 10M samples and report — how does p999 compare to p50?

---

## TOPIC 4 — Instrumentation Profilers

### Level 1 — Understand
- [ ] Open `code/03_profiling.cpp` and read the `ScopedTimer` implementation
- [ ] Answer: why do we use `std::chrono::steady_clock` and NOT `std::chrono::system_clock`?
- [ ] Answer: what does `__func__` give you? What is the difference from `__FUNCTION__`?
- [ ] What is the trade-off of inserting `ScopedTimer` into every function?
      (Hint: think about inlining and measurement overhead)

### Level 2 — Apply
- [ ] Extend `ScopedTimer` to output nanoseconds (not milliseconds) — verify it can time a 10-ns function
- [ ] Add a `ScopedTimer` to at least 3 functions in the code from Topics 1 & 2 — run and compare
- [ ] Implement a `MEASURE_FUNCTION()` macro that is a no-op when `NDEBUG` is defined
- [ ] Wrap two different sort algorithms (e.g., `std::sort` vs your own insertion sort) with
      `ScopedTimer` and compare on n=100,000 — what do you observe?

### Level 3 — HFT Challenge
- [ ] Implement `NanoTimer` using `__rdtsc()` instead of `std::chrono`:
      - Calibrate TSC cycles → nanoseconds using a known-duration sleep
      - Record start/stop TSC in a struct, compute delta in ns on destruction
      - Log to a `CircularBuffer<uint64_t, 1024>` — no stdout on the hot path
      - Prove that `NanoTimer` itself adds < 10 ns overhead by timing an empty block
- [ ] Use `NanoTimer` to profile the `order_book_lookup()` you wrote in Topic 1 —
      report the per-lookup latency distribution (min/p50/p99/max)

---

## TOPIC 5 — Sampling Profilers

### Level 1 — Understand
- [ ] Open `code/03_profiling.cpp`, read `section_sampling_profiler_simulation()`
- [ ] Answer: what does "Self%" mean in a sampling profiler output? What does "Total%" mean?
- [ ] Answer: why does f4() not appear in the profile in the book example?
- [ ] Answer: why would a mutex-blocked function NOT show up in a sampling profile?
- [ ] Name 3 real-world sampling profilers and their platforms (Linux, macOS, Windows)

### Level 2 — Apply
- [ ] Run `gprof` or `perf` on `code/01_big_o.cpp` — capture the call graph and explain the output
- [ ] Write a program where `f_hot()` runs 90% of the time and `f_cold()` runs 10% —
      confirm `perf report` or gprof shows f_hot with ~90% Self%
- [ ] Identify a bottleneck in the Topic 2 benchmark using a profiler — does it match your manual measurement?

### Level 3 — HFT Challenge
- [ ] Profile the `PreallocatedQueue` vs `std::deque` benchmark from Topic 2 with `perf stat` —
      report: cycles, cache-misses, cache-references, instructions per cycle (IPC)
      - Which has better IPC? More cache misses? Explain why from first principles
- [ ] Read the output of `perf record` + `perf annotate` on your order_book_lookup function —
      find the instruction that causes the most stalls and explain why

---

## TOPIC 6 — Cache Effects & Memory Locality

### Level 1 — Understand (Read + trace)
- [ ] Open `code/04_cache_effects.cpp` and read all 5 sections
- [ ] Answer: what is a cache line? How many `int`s fit in a single 64-byte cache line?
- [ ] Trace `section_cache_thrashing()`: why is column-major traversal ~20× slower than row-major?
      Draw how memory is laid out for `matrix[i][j]` vs `matrix[j][i]`
- [ ] Answer: what is the difference between spatial locality and temporal locality?
      Give one code example of each
- [ ] Fill in the latency table from memory: L1 cache ≈ ___ ns, L2 ≈ ___ ns, RAM ≈ ___ ns
- [ ] Answer: why is `std::vector` traversal faster than linked list traversal even when both have N elements?

### Level 2 — Apply (Write code)
- [ ] Run `code/04_cache_effects.cpp` and record the actual timings — do they match the expected ratios?
- [ ] Write `stride_experiment()`: iterate over a large array with stride 1, 4, 16, 64, 256
      — measure ns/element for each stride — at what stride does throughput collapse? Why?
- [ ] Implement `matrix_multiply_naive()` vs `matrix_multiply_transposed()` (transpose B first)
      — benchmark on 512×512 matrices and explain the cache behaviour
- [ ] Write a function that proves temporal locality: sum a small buffer (fits L1) vs large buffer
      (exceeds L3) repeatedly — compare ns/element

### Level 3 — HFT Challenge
- [ ] Implement an `OrderBookSoA` (Struct of Arrays) vs `OrderBookAoS` (Array of Structs):
      - AoS: `struct PriceLevel { double bid; double ask; int qty; }`
      - SoA: `struct Book { vector<double> bids, asks; vector<int> qtys; }`
      - Benchmark: scan only `bids` for best price across 1M levels — measure the SoA speedup
      - Explain in terms of cache lines: how many bytes wasted per cache line in AoS vs SoA?
- [ ] Design a `CacheAlignedPool<T, N>`: a pre-allocated pool where each element is aligned to 64 bytes
      — use `alignas(64)` and `std::array<std::aligned_storage_t<sizeof(T), 64>, N>`
      — benchmark sequential access vs the standard `std::vector<T>` allocation
- [ ] Run `perf stat -e cache-misses,cache-references` on your SoA vs AoS benchmark
      — report the cache miss ratio and correlate it with the timing results

---

## END-OF-CHAPTER INTEGRATION TASKS

- [ ] Build all code files: `g++ -std=c++20 -O2 -Wall -Wextra code/01_big_o.cpp -o big_o`
      (repeat for each file, or build individually since they each have `main()`)
- [ ] Build with profiling flags: `g++ -std=c++20 -O2 -pg code/01_big_o.cpp -o profiled_build`
- [ ] Write a single `performance_audit.cpp` that:
      - Runs `linear_search`, `binary_search`, and `insertion_sort` at 5 input sizes
      - Collects timings and prints a table confirming the Big O growth rates
      - Identifies the crossing point where binary search definitively beats linear search
- [ ] In `EXERCISES.md`, write 3 interview Q&A pairs per topic (18 total)
      Example: "Q: What is the amortized complexity of `push_back`? Why? A: O(1) — capacity doubles..."
- [ ] Push code to `gh:sh-arka22/Low-Latency-CPP-Exercises` with commit:
      `ch04: [topic] brief description`
