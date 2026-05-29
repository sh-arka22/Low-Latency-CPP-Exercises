# Chapter 7 — Memory Management
> C++ High Performance (2nd Ed.) | Björn Andrist & Viktor Sehr
> Repo folder: `cpp-high-performance/08-memory-management/`

---

## BFS CONCEPT MAP (start here — breadth first)

```
Memory Management
├── 1. Stack vs Heap
│   ├── Stack  — SP-based, ~0 ns, limited (8 MB), LIFO
│   ├── Heap   — malloc/free, 50–500 ns, fragmentation, unlimited
│   ├── BSS    — zero-init globals/statics
│   └── Data   — non-zero-init globals
│
├── 2. Smart Pointers
│   ├── unique_ptr<T>        — sole owner, move-only, ~8 B, ~0 ns move
│   ├── shared_ptr<T>        — ref-counted, 16 B, atomic inc/dec ~50 ns
│   ├── weak_ptr<T>          — non-owning observer, breaks cycles
│   └── intrusive_ptr        — ref count inside T, 8 B total, ~5 ns
│
├── 3. Custom Allocators
│   ├── Old model  — template param: std::vector<T, Alloc>
│   ├── PMR model  — std::pmr::polymorphic_allocator (C++17)
│   │   ├── monotonic_buffer_resource  — bump pointer, no free
│   │   ├── unsynchronized_pool_resource — free-list, single thread
│   │   └── synchronized_pool_resource   — free-list, thread-safe
│   └── Custom     — StackAllocator, PoolAllocator, ArenaAllocator
│
├── 4. Memory Alignment
│   ├── alignof(T)           — natural alignment
│   ├── alignas(N)           — force alignment
│   ├── struct padding       — compiler inserts padding bytes
│   ├── cache-line alignment — alignas(64) for false-sharing prevention
│   └── std::assume_aligned  — hint for autovectorizer (C++20)
│
├── 5. Placement New
│   ├── new (ptr) T(args)    — construct in pre-allocated storage
│   ├── ptr->~T()            — manual destructor call
│   ├── std::launder(ptr)    — fix pointer provenance after reuse
│   └── trivially copyable   — memcpy is safe; no UB
│
├── 6. Memory Pools & Arenas
│   ├── Bump-pointer arena   — ptr++, reset() to free all, O(1)
│   ├── Slab/Pool allocator  — fixed-size free list, O(1) alloc/dealloc
│   ├── jemalloc arenas      — per-thread, size-class bins
│   └── std::pmr resources   — polymorphic versions of the above
│
├── 7. Small Buffer Optimization (SBO/SSO)
│   ├── std::string SSO      — 15 chars inline (libstdc++), no heap
│   ├── std::function SBO    — small lambdas inline, large → heap
│   ├── std::any             — 3-ptr inline buffer
│   └── SmallVector<T,N>     — N elements inline, heap on overflow
│
├── 8. Cache Lines & False Sharing
│   ├── Cache line = 64 B (x86) / 128 B (Apple Silicon)
│   ├── False sharing        — different vars, same line, coherence storm
│   ├── hardware_destructive_interference_size — C++17 constant
│   ├── Prefetch             — __builtin_prefetch, hardware stride detect
│   └── NUMA                 — socket-local memory, numa_alloc_onnode
│
└── 9. RAII
    ├── Ctor acquires, dtor releases — all exit paths covered
    ├── Rule of Five         — destructor, copy ctor, copy=, move ctor, move=
    ├── lock_guard / scoped_lock
    └── Scope guards         — scope_exit, defer idiom
```

---

## SECTION 1 — Stack vs Heap

**The mental model:**

```
High address
┌──────────────────────────────┐
│           STACK              │  ← SP (Stack Pointer) grows down
│   (local vars, call frames)  │     ~0 ns, 8 MB default on Linux
├──────────────────────────────┤
│          (gap)               │
├──────────────────────────────┤
│         MMAP region          │  ← mmap() / large malloc()
├──────────────────────────────┤
│           HEAP               │  ← brk() / sbrk() / malloc()
├──────────────────────────────┤
│     BSS (zero-init statics)  │
├──────────────────────────────┤
│     Data (non-zero statics)  │
├──────────────────────────────┤
│     Text (code)              │
└──────────────────────────────┘
Low address
```

**Why stack is fast:**
- Allocation = `sub rsp, N` (one instruction, ~0.3 ns)
- No bookkeeping, no lock, no free list
- Always cache-hot (top of stack is in L1)

**Why heap is slow:**
- `malloc` must search free list → O(1) amortized but with constant factor
- May call kernel (`brk`/`mmap`) on first allocation or after OS reclaim
- First-touch page fault: ~1–10 µs on Linux (page must be zeroed by kernel)
- Internal + external fragmentation accumulates over time

**Key numbers (Intel Xeon E5):**
| Allocation | Latency |
|---|---|
| Stack (sub rsp) | ~0.3 ns |
| tcmalloc (thread-local cache hit) | ~15 ns |
| glibc malloc (free list) | ~50 ns |
| malloc → OS page fault | ~1–10 µs |

---

## SECTION 2 — Smart Pointers

### unique_ptr

```cpp
// sizeof == 8 bytes (just a pointer, default deleter is zero-size)
std::unique_ptr<Order> p = std::make_unique<Order>(id, price);
// Move is a pointer swap: ~0.3 ns
auto q = std::move(p);  // p is now null
```

**Control path:** constructor calls `::operator new`, destructor calls `::operator delete`. Move is `ptr = other.ptr; other.ptr = nullptr;`.

### shared_ptr (internals)

```
shared_ptr<T>:
  ┌─────────┐  ┌──────────────────────────────┐
  │  ptr    │→ │  T object                    │
  └─────────┘  └──────────────────────────────┘
  │ ctrl_ptr│→ ┌──────────────────────────────┐
  └─────────┘  │ use_count  (atomic<int>)     │
               │ weak_count (atomic<int>)     │
               │ deleter                      │
               │ allocator                    │
               └──────────────────────────────┘

sizeof(shared_ptr<T>) == 16  (two raw pointers)
```

**make_shared vs new:** `make_shared<T>(...)` fuses the T and control block into ONE allocation — better cache locality, one fewer `malloc`. `shared_ptr<T>(new T(...))` makes TWO allocations.

**Atomic cost:** `use_count` is `std::atomic<int>`. On x86, incrementing it emits a `lock xadd` instruction, which flushes the cache line from all other cores (~40–100 ns on a 2-socket NUMA system). This is why `shared_ptr` is dangerous on hot paths.

### weak_ptr

Provides a non-owning handle. `weak_ptr::lock()` atomically checks `use_count > 0` and bumps it. Used to break `shared_ptr` cycles (parent ↔ child).

**Rule of thumb for HFT:**
- On-hot-path: by-value or `unique_ptr`
- Off-hot-path (shared ownership): `shared_ptr` is fine
- Never: raw owning pointer

---

## SECTION 3 — Custom Allocators

### PMR (C++17) — the clean model

```cpp
// 1. Create a buffer on the stack
char buf[65536];
std::pmr::monotonic_buffer_resource arena{buf, sizeof(buf)};

// 2. Attach a PMR vector to it
std::pmr::vector<Order> orders{&arena};
orders.reserve(1000);           // allocates from arena (bump ptr)

// NO heap calls during the loop:
for (auto& msg : feed) orders.emplace_back(parse(msg));

// Free entire arena in one shot:
// (when 'arena' goes out of scope OR call arena.release())
```

**Monotonic buffer resource** — bump-pointer only, no individual deallocation:
```
[used|used|used|free.....................]
             ↑ bump ptr moves right on each alloc
```

**Pool resource** — fixed-size free list per size class:
```
Pool for size 64:  [slot]→[slot]→[slot]→nullptr
Pool for size 128: [slot]→[slot]→nullptr
```

### Old-style template allocator

```cpp
template <typename T>
struct PoolAlloc {
    using value_type = T;
    T* allocate(std::size_t n);
    void deallocate(T* p, std::size_t n);
};
std::vector<Order, PoolAlloc<Order>> orders;
```

Cons: doesn't propagate across rebind easily. PMR is the modern replacement.

---

## SECTION 4 — Memory Alignment

### The padding rule

**Compiler rule:** each member is placed at the next address that is a multiple of `alignof(member)`.

```cpp
struct Bad {
    char  a;   // offset 0,  align 1
               // 3 bytes padding
    int   b;   // offset 4,  align 4
    char  c;   // offset 8,  align 1
               // 3 bytes padding (struct size must be multiple of max align)
};
// sizeof(Bad) == 12   (wasted 6 bytes!)

struct Good {
    int   b;   // offset 0,  align 4
    char  a;   // offset 4,  align 1
    char  c;   // offset 5,  align 1
               // 2 bytes padding
};
// sizeof(Good) == 8   (only 2 bytes wasted)
```

**Golden rule:** sort fields largest-to-smallest alignment.

### Cache-line alignment

```cpp
struct alignas(64) HotData {
    std::atomic<int64_t> bid_price;
    std::atomic<int64_t> ask_price;
    // fills 64 bytes so ColdData is on a different line
    char _pad[48];
};

struct ColdData {
    uint64_t order_count;
    // ...
};
```

### C++20: `std::assume_aligned`

```cpp
void process(float* data, int n) {
    float* p = std::assume_aligned<32>(data);  // hint: 32-byte aligned
    // Compiler can now emit AVX2 loads (non-masked)
    for (int i = 0; i < n; ++i) p[i] *= 2.0f;
}
```

---

## SECTION 5 — Placement New

### Motivation

In a memory pool or ring buffer, you want to **reuse** a fixed storage location without calling `malloc/free`. Placement new separates storage allocation from object construction.

```cpp
alignas(Order) char buf[sizeof(Order)];

// Construct into pre-existing storage:
Order* p = new (buf) Order{id, price, qty};

// Manually destroy (doesn't free buf):
p->~Order();

// Reuse for a new object:
p = new (buf) Order{id2, price2, qty2};
```

### std::launder (C++17)

Without `launder`, the compiler is allowed to assume the object identity at a pointer never changes — meaning it can cache reads through the pointer. After you destroy and reconstruct via placement new, you must `launder`:

```cpp
p->~Order();
new (buf) Order{id2, price2, qty2};
Order* p2 = std::launder(reinterpret_cast<Order*>(buf));
// p2 is now a valid pointer to the new Order
```

### Trivially copyable

If `std::is_trivially_copyable_v<T>`, then `memcpy` is safe (no UB). No destructor call needed. The compiler can optimise it to a single load/store or SIMD move.

---

## SECTION 6 — Memory Pools and Arenas

### Bump-pointer arena

```
Initial:   [___________________________________________] 64 KB buffer
After 3×:  [Order|Order|Order|____________________________]
                                ↑ bump ptr

Deallocation: impossible individually. Reset() moves bump ptr back to start.
```

**Time complexity:**
- `allocate(n)` — O(1): `ptr += n; return old_ptr;`
- `reset()` — O(1): `ptr = begin;`
- `deallocate(p, n)` — no-op (or `assert(false)`)

### Slab/pool allocator

```
Free list for sizeof(Order) == 64:
head → [next→][next→][next→] nullptr

allocate() → pop head (O(1))
deallocate(p) → push p to head (O(1))
```

**Key insight:** you never call the OS after the initial `mmap`. Zero kernel crossings on the hot path = predictable latency.

### Production: tcmalloc / jemalloc

Both use per-thread caches of size-class free lists. `malloc(64)` is a pop from the thread-local cache — no lock, no syscall, ~15 ns. Falls back to central heap (locked) only when cache is empty.

---

## SECTION 7 — Small Buffer Optimization

### SSO in std::string

```
libstdc++ std::string layout (24 bytes):

Small mode (len ≤ 15):
  ┌─────────────────────────────────────┐
  │ char buf[16]          (inline data) │
  │ uint8_t len           (in last byte)│
  └─────────────────────────────────────┘

Large mode (len > 15):
  ┌─────────────────────────────────────┐
  │ char* ptr             (8 B pointer) │
  │ size_t size           (8 B)         │
  │ size_t capacity       (8 B)         │
  └─────────────────────────────────────┘
```

The branch "is it small?" happens on every access but is branch-predicted nearly perfectly since trading symbols are always short.

### std::function SBO

`std::function<void()>` stores:
1. A vtable-like invoker pointer
2. A storage buffer (typically 24–32 bytes)
3. If the callable fits in the buffer → in-place (no heap)
4. If it overflows → heap allocation

This is why large captures (`[this, large_vector]`) in `std::function` are expensive on hot paths.

### SmallVector<T, N>

Used by LLVM, Abseil (`absl::InlinedVector`). Stores first N elements inline, heap on overflow. For N=8, a `SmallVector<Order*, 8>` fits in ~80 bytes on the stack — ideal for small order lists without heap.

---

## SECTION 8 — Cache Lines, False Sharing, Prefetch

### False sharing in detail

```
CPU 0                          CPU 1
thread A: counter_a++    |    thread B: counter_b++

Memory layout (bad):
[counter_a | counter_b]   ← same 64-byte cache line

Timeline:
  t=0:  A reads line (M state on CPU0)
  t=1:  B wants to write → sends RFO (Request For Ownership) to CPU0
  t=2:  CPU0 must flush + invalidate → ~100-400 ns stall
  t=3:  B writes (M state on CPU1)
  t=4:  A wants to write → sends RFO to CPU1 ...
  → coherence ping-pong, effective throughput ~1/400 ns = 2.5 M ops/sec
     instead of 3 GHz = 3 B ops/sec
```

Fix:
```cpp
struct alignas(64) Counter {
    std::atomic<uint64_t> value{0};
    char _pad[56];  // fill rest of cache line
};
Counter counter_a, counter_b;  // now on different lines
// → coherence traffic eliminated, 50–200× throughput improvement
```

### hardware_destructive_interference_size

C++17 added `std::hardware_destructive_interference_size` — returns the minimum distance two objects must be apart to avoid false sharing. On x86 it's 64, on Apple Silicon it's 128. **Always use this constant**, not the hardcoded 64.

### Hardware prefetcher

The CPU's stride prefetcher detects linear access patterns and prefetches ahead. Walking a `std::vector<T>` at stride-1 → prefetcher kicks in immediately, effective cache miss rate → ~0. Walking a linked list → random pointer chasing → 100% cache miss rate → ~200 ns per node.

Manual prefetch:
```cpp
for (size_t i = 0; i < n; ++i) {
    __builtin_prefetch(&data[i + 16], 0, 1);  // prefetch 16 ahead
    process(data[i]);
}
```

---

## SECTION 9 — RAII

### Core pattern

```cpp
class MmapRegion {
    void*  ptr_;
    size_t len_;
public:
    MmapRegion(size_t len) : len_{len} {
        ptr_ = mmap(nullptr, len, PROT_READ|PROT_WRITE,
                    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (ptr_ == MAP_FAILED) throw std::runtime_error("mmap failed");
    }
    ~MmapRegion() noexcept { munmap(ptr_, len_); }

    // Move-only (like unique_ptr)
    MmapRegion(const MmapRegion&) = delete;
    MmapRegion& operator=(const MmapRegion&) = delete;
    MmapRegion(MmapRegion&& o) noexcept : ptr_{o.ptr_}, len_{o.len_} { o.ptr_ = MAP_FAILED; }
    MmapRegion& operator=(MmapRegion&& o) noexcept { std::swap(ptr_, o.ptr_); std::swap(len_, o.len_); return *this; }

    void* data() noexcept { return ptr_; }
    size_t size() const noexcept { return len_; }
};
```

If constructor throws after partial acquisition, the already-acquired resource can still be released by a RAII member. C++ guarantees destructors of fully-constructed members run even when the constructor throws.

### Rule of Five checklist

If you define any of: destructor, copy ctor, copy assignment, move ctor, move assignment — then you should define or explicitly `= delete` / `= default` ALL FIVE.

### Latency-timer RAII (HFT pattern)

```cpp
struct ScopedTimer {
    uint64_t start;
    const char* label;
    ScopedTimer(const char* l) : start{__rdtsc()}, label{l} {}
    ~ScopedTimer() {
        uint64_t elapsed = __rdtsc() - start;
        // write to lock-free histogram — zero heap, no exceptions
        LatencyHistogram::record(label, elapsed);
    }
};

void process_order(Order& o) {
    ScopedTimer t{"process_order"};
    // ... work ...
}   // ← destructor fires here unconditionally
```

---

## TOP 25 HFT INTERVIEW QUESTIONS

1. Why is `malloc` non-deterministic in latency?
2. What is the sizeof `unique_ptr<int>`? `shared_ptr<int>`? Why the difference?
3. What is the control block in `shared_ptr`? What is in it?
4. Why is `make_shared` preferred over `shared_ptr(new T(...))`?
5. What CPU instruction does `shared_ptr` copy emit? Why does it take ~50 ns on multi-core?
6. What is false sharing? Write the benchmark to prove it.
7. What is `hardware_destructive_interference_size` and why is it not always 64?
8. What is `std::pmr::monotonic_buffer_resource`? What is its deallocation strategy?
9. Explain bump-pointer allocation. What is O(allocation)? O(deallocation)?
10. What is SSO? What is sizeof `std::string` on libstdc++?
11. Explain placement new. When must you call `ptr->~T()` manually?
12. What is `std::launder`? When is it needed?
13. What struct layout maximises packing? Give an example.
14. What is `alignas(64)` and when do you use it?
15. What is `std::assume_aligned`? What optimisation does it unlock?
16. Why is walking a linked list 50–100× slower than walking a vector?
17. What is the cache miss penalty at each level? (L1/L2/L3/RAM in cycles)
18. How does tcmalloc achieve ~15 ns allocation? What is the fast path?
19. What is internal vs external fragmentation?
20. Explain the Rule of Five. When does the compiler generate them for you?
21. What is a scope guard / `scope_exit`? How does it differ from RAII?
22. Why is `std::function` slow on the hot path? What do you replace it with?
23. What is `absl::InlinedVector` / `llvm::SmallVector`? When do you use it?
24. Explain NUMA. Why does memory location matter on a 2-socket server?
25. How do you achieve zero `malloc` calls in a market-data decode loop?
