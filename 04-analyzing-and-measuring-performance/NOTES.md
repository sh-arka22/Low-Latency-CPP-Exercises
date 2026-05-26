# Chapter 3 — Measuring Performance
> C++ High Performance (2nd Ed.) | Björn Andrist & Viktor Sehr
> Repo folder: `cpp-high-performance/04-analyzing-and-measuring-performance/`

---

## BFS CONCEPT MAP (start here — breadth first)

```
Measuring Performance
├── 1. Asymptotic Complexity & Big O
│   ├── Growth rate functions (O(1), O(log n), O(n), O(n log n), O(n²), O(2^n))
│   ├── Worst / Best / Average case
│   └── How to derive Big O from code (count loops + eliminate low-order terms)
├── 2. Amortized Complexity
│   └── std::vector::push_back — O(1) amortized despite occasional O(n) resize
├── 3. Performance Properties (vocabulary)
│   ├── Latency vs Throughput
│   ├── CPU-bound vs I/O-bound vs Memory-bound
│   └── Percentiles vs Mean (why median is more useful)
├── 4. Performance Testing Best Practices
│   ├── Pareto Principle — 80% of time in 20% of code
│   └── Test with realistic data; plot your data
└── 5. Profilers
    ├── Instrumentation profiler — ScopedTimer, __func__, #define MEASURE_FUNCTION()
    └── Sampling profiler — call stack snapshots at ~10ms intervals; Self% vs Total%
```

---

## SECTION-BY-SECTION DEEP NOTES

### 1 — Asymptotic Complexity & Big O

**Core idea:** We want to express how runtime or memory *scales* with input size, independent of machine speed, constant factors, or low-order terms.

**How to derive Big O from code:**
1. Identify the loops / recursion that depend on `n`
2. Count total iterations in terms of `n`  
3. Keep only the highest-order term
4. Drop the constant factor

**Common growth rates (memorize this table):**

| f(n) | Name | n=10 | n=50 | n=1000 |
|------|------|------|------|--------|
| O(1) | Constant | 0.001 s | 0.001 s | 0.001 s |
| O(log n) | Logarithmic | 0.003 s | 0.006 s | 0.01 s |
| O(n) | Linear | 0.01 s | 0.05 s | 1 s |
| O(n log n) | Linearithmic | 0.03 s | 0.3 s | 10 s |
| O(n²) | Quadratic | 0.1 s | 2.5 s | 16.7 min |
| O(2ⁿ) | Exponential | 1 s | 357 centuries | ☠️ |

*Assuming 1 ms = 1 unit of work.*

**Book examples:**
- `linear_search(vector<int>)` → O(n) — scan all elements worst case
- `linear_search(vector<Point>)` → still O(n) — constants differ, growth rate same
- `binary_search(sorted vector<int>)` → O(log n) — halve search space each step
- `insertion_sort(vector<int>)` → O(n²) — outer loop × inner: sum 1+2+…+(n-1) = n²/2

**Key rule from the book:**
> *Never spend time tuning your code before you are certain that you have chosen the correct algorithms and data structures.*

### 2 — Amortized Complexity

**Core idea:** Some operations are occasionally expensive but cheap on average when spread over many calls.

**`std::vector::push_back` anatomy:**
- If `size < capacity` → O(1): write element, increment size
- If `size == capacity` → O(n): allocate 2× buffer, move all elements, then write

**Key insight:** The capacity doubles on each resize. So for n total pushes, total work = n + n/2 + n/4 + … = 2n → **O(1) amortized per push.**

**Amortized ≠ Average:**
- *Average* = expected cost given a distribution of inputs
- *Amortized* = guaranteed average cost over a sequence of operations regardless of input

**STL contract:** All STL container/iterator specs use amortized complexity (not worst-case per call).

### 3 — Performance Properties (Vocabulary)

| Term | Definition |
|------|-----------|
| **Latency** | Time from request to response (one operation) |
| **Throughput** | Operations processed per unit time |
| **CPU-bound** | Would run faster with a faster CPU |
| **I/O-bound** | Would run faster with faster disk/network |
| **Memory-bound** | Bottleneck is RAM speed or capacity |
| **Power consumption** | Especially relevant on battery devices; avoid busy-polling |

**Aggregating samples:**
- Mean = easy to compute, sensitive to outliers
- Median = robust to outliers (usually more informative)
- Percentiles (p50, p99, p999) = reveals tail latency — critical for HFT

### 4 — Performance Testing Best Practices

**Pareto Principle (80/20 rule):**
> 80% of execution time lives in 20% of the code. Find that 20% — don't optimize blindly.

**Guidelines from the book:**
1. Measure early — add perf tests to nightly CI builds
2. Test with realistic data sizes (not toy 100-element examples)
3. **Plot your data** — outliers and patterns invisible in a table show up in a graph
4. Choose correct algorithms/data structures before micro-optimizing

### 5 — Profilers

#### 5a — Instrumentation Profilers

**Mechanism:** Insert timing code at function entry/exit.

**Book's `ScopedTimer` class (key points):**
```cpp
class ScopedTimer {
    using ClockType = std::chrono::steady_clock;  // monotonic — never decreases
    // ...
    ~ScopedTimer() { /* measure duration, print */ }
};
```
- Use `steady_clock` (not `system_clock`) — steady_clock is monotonic; system_clock can be adjusted by NTP mid-measurement
- Use `__func__` (C++11 standard) for function name in macros
- Conditional macro `#define MEASURE_FUNCTION()` — zero cost when disabled

**Tradeoffs:**
- Pro: Exact per-function timing
- Con: Inserted code changes the performance being measured; may block inlining

#### 5b — Sampling Profilers

**Mechanism:** Every ~10 ms, capture a snapshot of the call stack.

**Key metrics:**
- **Total%** — % of samples that contained this function anywhere in the stack
- **Self%** — % of samples where this function was *on top* (actively executing)

**Example from book:**
```
Function   Total   Self
main()     100%    10%     ← rarely the bottleneck itself
f1()       80%     10%
f2()       70%     30%     ← called often, does real work
f3()       50%     50%     ← hottest function — optimize here
```

**Critical limitations to know:**
1. Short functions called infrequently may **never appear** in samples (like `f4()` in book example)
2. **Sleeping threads are invisible** — synchronization/lock contention won't show up in sampling profiles
3. Statistical nature — need sufficient sample count for accuracy

**Tools:**
- `gprof` — hybrid (instrumentation + sampling), classical Unix tool
- `perf` (Linux) — hardware counter-based sampling profiler
- `Valgrind/Callgrind` — instrumentation-based (very accurate but 10-50× slowdown)
- `Intel VTune`, `Apple Instruments` — production-grade GUI profilers

---

## HFT OVERLAY (not in book — added for your context)

### Why every line of Chapter 3 matters in a trading system:

**Big O in HFT:**
- Order book update must be O(log n) at worst (tree-based book) or O(1) (hash-based)
- Any O(n) on the hot path (feed handler → order router) is unacceptable at high message rates
- `std::map` lookup = O(log n); `std::unordered_map` lookup = O(1) amortized — this is not a trivial choice at 10M msgs/sec

**Amortized in HFT:**
- `push_back` on the hot path is dangerous if resize can happen mid-tick — **always `reserve()` upfront**
- Use memory pools / pre-allocated ring buffers to guarantee O(1) worst-case, not just amortized

**Latency vs Throughput in HFT:**
- Market makers care about *latency* (react to price change before competitors)
- Risk engines care about *throughput* (validate N orders per second)
- These are often at odds — you trade off one for the other

**Profiling HFT systems:**
- Sampling profilers miss the ~100 ns hot path — use **hardware performance counters** (`perf stat`, `RDTSC`)
- `steady_clock` has ~20 ns overhead itself — use `__rdtsc()` for sub-nanosecond timing
- Sleeping threads invisible in sampling profilers → **lock contention won't surface** — use lock-free structures and profilers that track scheduler events (`perf sched`, `BPF tracing`)

**The 80/20 rule in a quote engine:**
- 80% of latency typically lives in: serialization/deserialization, memory allocation, and cache misses
- Profile first, then eliminate each of these in order of impact

---

## CONNECTIONS TO PREVIOUS CHAPTERS

- **Ch1**: `vector<Car>` contiguous memory → O(1) spatial locality (cache-friendly iteration) vs `ArrayList<Car>` in Java → pointer chase = O(n) cache misses
- **Ch2**: `noexcept` on move ctor → `vector` resize uses move (O(1) per element) vs copy (potentially O(n) work per element) — this directly links to amortized complexity
- **Ch2**: RAII `ScopedTimer` from this chapter is *exactly* the RAII pattern from Ch1/Ch2

---

## KEY QUOTES TO REMEMBER

> *"Never spend time tuning your code before you are certain that you have chosen the correct algorithms and data structures."* — Book, Ch3

> *"20% of the code is responsible for 80% of the resources."* — Pareto Principle

> *"Conceptually, a sampling profiler stores samples of call stacks at even time intervals. Pure sampling profilers usually only detect functions that are currently being executed in a thread that is in a running state, since sleeping threads do not get scheduled on the CPU."* — Book, Ch3
