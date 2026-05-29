# Chapter 7 — TODO Task List
> C++ High Performance (2nd Ed.) | **Memory Management**
> Folder: `cpp-high-performance/08-memory-management/`
> Linked code: `code/01_stack_vs_heap.cpp`, `code/02_smart_pointers.cpp`, `code/03_custom_allocator.cpp`, `code/04_memory_alignment.cpp`, `code/05_placement_new.cpp`, `code/06_memory_pool_arena.cpp`, `code/07_small_buffer_optimization.cpp`, `code/08_cache_lines_false_sharing.cpp`
> Linked repos: [PacktPublishing/Cpp-High-Performance-Second-Edition](https://github.com/PacktPublishing/Cpp-High-Performance-Second-Edition) · [ITHelpDec/CPP-High-Performance](https://github.com/ITHelpDec/CPP-High-Performance) · [sh-arka22/Low-Latency-CPP-HighPerformance](https://github.com/sh-arka22/Low-Latency-CPP-HighPerformance)

---

## ✅ HOW TO USE THIS FILE
- **BFS first** — sweep every topic at **Level 1** so you have a working concept map.
- **Then DFS** — drop into **Level 2** (apply) and **Level 3** (HFT challenge) for the topics that matter most for trading.
- Tick `[ ]` → `[x]` as you go. Each topic has an "interviewer would ask" prompt.

---

## TOPIC 1 — Stack vs Heap Memory

### Level 1 — Understand
- [ ] Draw the process memory layout from top to bottom: stack, mmap region, heap, BSS, data, text.
- [ ] Explain why stack allocation is ~0 ns and heap allocation is 50–500 ns. What is the SP register doing?
- [ ] What is the maximum stack size on Linux? (default 8 MB). What causes a stack overflow?
- [ ] When does `new`/`delete` call the OS (`sbrk`/`mmap`) vs just returning from the free list?
- [ ] Define **heap fragmentation** (internal vs external). Why does fragmentation grow over time?

### Level 2 — Apply
- [ ] Open `code/01_stack_vs_heap.cpp`. Measure the latency of stack-allocated vs heap-allocated objects in a tight loop.
- [ ] Confirm `sizeof` for stack vs pointer-indirected objects. Show the cost of an extra dereference.
- [ ] Write a function that allocates a 2 MB array on the stack — observe segfault vs increase ulimit.

### Level 3 — HFT Challenge
- [ ] Audit your order-event struct. Can it fit in 64 bytes (one cache line)? Can it live on the stack for intra-function processing?
- [ ] Replace a heap-allocated `Order*` chain with a stack-local `std::array<Order, 8>` for the hot path.

> **Interviewer asks:** "Why is `malloc` slow on a hot path? Name three alternatives used in production trading systems."

---

## TOPIC 2 — Smart Pointers (unique_ptr, shared_ptr, weak_ptr)

### Level 1 — Understand
- [ ] State the ownership semantics of each: `unique_ptr` (sole owner), `shared_ptr` (shared ref-count), `weak_ptr` (non-owning observer).
- [ ] What is the sizeof each pointer on x86-64? (`unique_ptr<T>` ≈ 8 B with default deleter, `shared_ptr<T>` = 16 B — two pointers).
- [ ] What is the **control block** in `shared_ptr`? Where does it live? What is in it?
- [ ] Why is `make_shared` preferred over `shared_ptr(new T(...))`? (One allocation, cache locality.)
- [ ] Explain why `shared_ptr` is **unsafe in signal handlers** and **problematic in lock-free code**.
- [ ] What is `std::owner_less`? Why do you need it for `weak_ptr` in maps?

### Level 2 — Apply
- [ ] Open `code/02_smart_pointers.cpp`. Benchmark `unique_ptr` move vs `shared_ptr` copy (ref-count bump) — expect ~50 ns penalty for shared_ptr on multi-core.
- [ ] Write a `shared_ptr` cycle and show the memory leak. Fix it with `weak_ptr`.
- [ ] Demonstrate custom deleters: `unique_ptr<FILE, decltype(&fclose)>`.

### Level 3 — HFT Challenge
- [ ] Audit your order-book: wherever you have `shared_ptr<Order>`, replace with intrusive reference counting (`boost::intrusive_ptr`) or better: by-value. Measure the latency difference.
- [ ] Build a `UniquePtr<T, PoolDeleter>` that returns memory to a pool instead of `free`.

> **Interviewer asks:** "Why does `shared_ptr` add ~50 ns on a multi-core machine? What is the exact CPU instruction responsible?"

---

## TOPIC 3 — Custom Allocators

### Level 1 — Understand
- [ ] Explain the C++ allocator concept: `allocate(n)`, `deallocate(p, n)`, `construct(p, args...)`, `destroy(p)`.
- [ ] Why do standard containers accept an allocator template parameter?
- [ ] What is **allocator propagation**? When does `std::vector<T, A>::operator=` propagate the allocator?
- [ ] What is `std::pmr::polymorphic_allocator` (C++17)? How does it differ from the old template-based allocator?
- [ ] Name three `std::pmr` memory resources: `monotonic_buffer_resource`, `unsynchronized_pool_resource`, `synchronized_pool_resource`.

### Level 2 — Apply
- [ ] Open `code/03_custom_allocator.cpp`. Implement a `StackAllocator<T, N>` that uses a fixed-size buffer.
- [ ] Use `std::pmr::monotonic_buffer_resource` with a `std::vector<Order>` and compare allocation time vs default.
- [ ] Show how `std::pmr::vector` works as a drop-in replacement.

### Level 3 — HFT Challenge
- [ ] Implement a **per-thread arena allocator** for inbound market-data messages. Each message is allocated bump-pointer style; entire arena is reset at the end of each microsecond tick.
- [ ] Prove zero heap traffic during the hot path using `valgrind --tool=massif` or `/proc/self/status VmRSS` delta.

> **Interviewer asks:** "How does a monotonic/arena allocator achieve O(1) allocation? What is its dealloction model?"

---

## TOPIC 4 — Memory Alignment and Padding

### Level 1 — Understand
- [ ] Define **natural alignment**: `alignof(T)` = sizeof(T) for primitives up to 8 bytes.
- [ ] Explain how the compiler inserts **padding bytes** to satisfy alignment. Draw the layout of `struct Bad { char a; int b; char c; }` (12 bytes) vs `struct Good { int b; char a; char c; }` (8 bytes).
- [ ] What happens at the CPU level on an **unaligned access** on x86 (performance penalty) vs ARM (SIGBUS)?
- [ ] What is `alignas(N)`? When do you use `alignas(64)` on a struct?
- [ ] Explain `std::aligned_storage<Size, Align>` and `std::aligned_alloc`.

### Level 2 — Apply
- [ ] Open `code/04_memory_alignment.cpp`. Use `offsetof` and `sizeof` to expose struct padding. Reorder fields to eliminate it.
- [ ] Align a struct to a cache line: `struct alignas(64) CacheLine { ... }`. Verify with `static_assert(alignof(CacheLine) == 64)`.
- [ ] Use `__builtin_assume_aligned` (GCC) or `std::assume_aligned<N>` (C++20) to hint the autovectorizer.

### Level 3 — HFT Challenge
- [ ] Design an `Order` struct: 8-byte symbol, 8-byte price (int64), 4-byte qty, 4-byte side+flags. Show it fits in 24 bytes with no padding. Show aligned to 64 bytes with hot read fields in first 32 bytes.
- [ ] Benchmark a loop over `vector<UnalignedOrder>` vs `vector<AlignedOrder>` — measure the SIMD width the compiler generates (use `-O2 -march=native -S`).

> **Interviewer asks:** "A struct has a `bool` followed by a `double`. What is sizeof? Why? How do you fix it?"

---

## TOPIC 5 — Placement New and Manual Object Lifecycle

### Level 1 — Understand
- [ ] Explain placement new: `new (ptr) T(args...)`. What does it do that regular `new` does not?
- [ ] When must you manually call the destructor? `ptr->~T()`.
- [ ] What is the difference between **storage lifetime** and **object lifetime**? (Key for memory pools.)
- [ ] Why is `reinterpret_cast<T*>(buffer)` without placement new undefined behaviour?
- [ ] C++17: `std::launder` — when is it needed and why?

### Level 2 — Apply
- [ ] Open `code/05_placement_new.cpp`. Implement a `RingSlot<T>` that reuses a fixed buffer: construct via placement new, destroy manually, reuse the slot.
- [ ] Demonstrate the UB without `std::launder` and the correct form with it.
- [ ] Show how compilers reorder memory: without a `std::launder`, the compiler may cache the old value.

### Level 3 — HFT Challenge
- [ ] Implement a `SlotAllocator<T, N>` for a lock-free SPSC queue where slots are never freed — only destructed and reconstructed in-place. Zero heap calls after init.

> **Interviewer asks:** "Can you `memcpy` a C++ object? Under what conditions? What about trivially-copyable types?"

---

## TOPIC 6 — Memory Pools and Arena Allocation

### Level 1 — Understand
- [ ] Explain a **bump-pointer arena**: a contiguous buffer + a pointer that only moves forward. Allocation = pointer increment, O(1).
- [ ] Explain a **slab/pool allocator**: a free list of fixed-size slots. Allocation = pop from free list, deallocation = push back. O(1) both ways.
- [ ] What is **internal fragmentation** in a pool (wasted bytes inside allocated blocks)?
- [ ] What is the difference between `delete` and `destroy + deallocate` in the allocator model?
- [ ] Name two production arena allocators: jemalloc's arena, tcmalloc's size-class caches.

### Level 2 — Apply
- [ ] Open `code/06_memory_pool_arena.cpp`. Implement `ArenaAllocator<>` backed by a 1 MB buffer. Allocate 1M `Order` objects and measure time vs `new`.
- [ ] Implement `PoolAllocator<T>` using a free list (`T* next` in the free slot). Show O(1) alloc and dealloc.
- [ ] Use `std::pmr::monotonic_buffer_resource` for the same task. Compare.

### Level 3 — HFT Challenge
- [ ] Build a **message arena** for inbound FIX/UDP decode: pre-allocate 64 KB per socket thread, decode into arena, reset arena after each message burst. Achieve zero `malloc` calls in the hot path.
- [ ] Benchmark with `perf stat` or `valgrind --tool=callgrind`: confirm zero `_int_malloc` calls.

> **Interviewer asks:** "What is the time complexity of malloc? Why isn't it always O(1)?"

---

## TOPIC 7 — Small Buffer Optimization (SBO/SSO)

### Level 1 — Understand
- [ ] Explain **SSO** (Small String Optimization): `std::string` stores strings ≤15 chars in-place (libstdc++) or ≤22 chars (libc++) — no heap allocation.
- [ ] What is `sizeof(std::string)`? (24 bytes on libstdc++ GCC, 32 bytes on libc++ Clang.)
- [ ] Explain **SBO** generalized: `std::function`, `std::any`, `llvm::SmallVector<T,N>` all use the same trick.
- [ ] What is the performance impact of SBO? (No heap, cache-hot, branch for "is it small?")
- [ ] When does the SBO buffer overflow? What happens? (Heap allocation, data moved, old buffer invalidated.)

### Level 2 — Apply
- [ ] Open `code/07_small_buffer_optimization.cpp`. Implement a `SmallVec<T, N>` that stores ≤N elements inline, falls back to heap.
- [ ] Prove `sizeof(std::string)` and the SSO threshold on your compiler with a loop: what length triggers a heap allocation?
- [ ] Benchmark `std::function` with a small capturing lambda (stays in SBO buffer) vs a large capture (triggers heap).

### Level 3 — HFT Challenge
- [ ] Design a `SmallString<16>` for instrument symbols (always ≤8 chars like "AAPL", "MSFT", "EURUSD"). Zero heap, stack-resident, `==`/`<` comparable. Profile symbol lookup in a 10M-order stream.
- [ ] Replace `std::function<void(Order&)>` callbacks in your order handler with `SmallFunction<void(Order&), 32>` (32-byte SBO). Show the elimination of heap traffic.

> **Interviewer asks:** "What is SSO? What does `sizeof(std::string)` return? How do you check if a string is heap-allocated?"

---

## TOPIC 8 — Cache Lines, False Sharing, and Hardware Prefetch

### Level 1 — Understand
- [ ] State the cache line size: **64 bytes** on x86 (Intel/AMD), **128 bytes** on Apple Silicon (M1/M2/M3).
- [ ] Define **false sharing**: two threads write to different variables that happen to share a cache line → coherence traffic → ~100–400 ns stall.
- [ ] Explain `std::hardware_destructive_interference_size` (C++17). Why is it not always 64?
- [ ] What is **hardware prefetching**? Why does stride-1 iteration beat stride-16 iteration by 10×?
- [ ] What is a **cache miss penalty**? L1: ~4 cycles, L2: ~12 cycles, L3: ~40 cycles, RAM: ~200 cycles (on modern Intel).

### Level 2 — Apply
- [ ] Open `code/08_cache_lines_false_sharing.cpp`. Reproduce false sharing: two threads incrementing adjacent counters — measure throughput with and without padding.
- [ ] Add `alignas(std::hardware_destructive_interference_size)` and re-measure. Expect 5–20× speedup.
- [ ] Show `__builtin_prefetch(addr, 0, 3)` before a pointer-chasing loop. Measure latency delta.

### Level 3 — HFT Challenge
- [ ] In a multi-threaded order-book, separate **hot read fields** (bid/ask price) from **cold write fields** (order count, stats) into different cache lines. Benchmark read throughput on the hot path.
- [ ] Implement a **NUMA-aware allocator** hint: pin threads to NUMA nodes, allocate order-book memory on the same node. Use `numa_alloc_onnode` or `mbind`.

> **Interviewer asks:** "Two threads each write to a uint64_t 10M times. No data race (different variables). Why is it 10× slower than single-threaded? How do you fix it?"

---

## TOPIC 9 — RAII and Resource Management

### Level 1 — Understand
- [ ] Define RAII: resource acquisition in constructor, release in destructor. Name 4 resources it covers: memory, file handles, mutexes, sockets.
- [ ] Why is RAII **superior to try/finally** (Java/Python)? (Destructor runs on *all* exit paths including exception + no forgotten `finally` branch.)
- [ ] What is the **Rule of Five** (C++11)? When do you need it vs when can you rely on the compiler?
- [ ] What is the difference between `std::lock_guard`, `std::unique_lock`, and `std::scoped_lock`?
- [ ] What are **scope guards** (`std::experimental::scope_exit`, Boost.ScopeExit)?

### Level 2 — Apply
- [ ] Open existing `code/` files. Identify any raw resource acquisitions that are not RAII-wrapped. Fix them.
- [ ] Implement `FileHandle` (RAII wrapper for `FILE*`), `MmapRegion` (RAII for `mmap`/`munmap`), `TimedLock` (acquires mutex with timeout via RAII).
- [ ] Demonstrate: without RAII, early return leaks. With RAII, guaranteed cleanup.

### Level 3 — HFT Challenge
- [ ] Build a `ScopedLatencyTimer` that records start in ctor, computes elapsed in dtor, writes to a `LatencyHistogram`. Zero heap, `__rdtsc`-based.
- [ ] Design an RAII wrapper for a UDP socket: ctor opens + binds, dtor closes. Show it's exception-safe even if `bind` throws after `socket()`.

> **Interviewer asks:** "What is RAII? Why does C++ not need `finally` blocks? What is the Rule of Five?"
