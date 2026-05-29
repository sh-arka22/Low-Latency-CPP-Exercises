# Chapter 7 — Memory Management: HFT-Grade Deep Dive
> C++ High Performance (2nd Ed.) · Björn Andrist & Viktor Sehr
> Folder: `cpp-high-performance/08-memory-management/`
> Audience: quant/HFT C++ developer; sub-microsecond latency target

---

## 1. The Memory Hierarchy — Your First Principle

Everything in this chapter flows from one truth: **not all memory is equal in time**.

```
Register      ~0.3 ns   (compiler allocated)
L1 cache      ~1 ns     4 cycles, 32–64 KB per core
L2 cache      ~4 ns     12 cycles, 256 KB–1 MB per core
L3 cache      ~15 ns    40 cycles, 8–64 MB shared
DRAM          ~70 ns    200 cycles (local socket)
DRAM (remote) ~140 ns   400 cycles (remote NUMA socket)
SSD NVMe      ~20 µs
HDD           ~5 ms
```

Every design decision in this chapter is an attempt to stay in the top three rows.

---

## 2. Stack vs Heap — From First Principles

### 2.1 What the CPU actually does

On x86-64, the stack is defined by the RSP (stack pointer) register. Allocating N bytes on the stack is one instruction:

```asm
sub rsp, 64    ; allocate 64 bytes on the stack
```

Cost: **~0.3 ns** (one clock cycle).

Freeing is:
```asm
add rsp, 64    ; pop the frame
```

The heap is a data structure managed by `malloc` (or tcmalloc/jemalloc). It must find a free block of the right size, update bookkeeping, and potentially call the kernel if none exist.

### 2.2 Process memory layout (Linux x86-64)

```
0xFFFFFFFF FFFFFFFF  ← kernel space
...
0x00007FFF FFFFFFFF  ← top of user space
┌───────────────────────────────────────┐
│  STACK (grows ↓)                      │  8 MB default (ulimit -s)
│  local vars, call frames, alloca      │
├───────────────────────────────────────┤
│  ...gap...                            │
├───────────────────────────────────────┤
│  MMAP region                          │  large mallocs (>128 KB) → mmap
│  shared libs, file maps               │
├───────────────────────────────────────┤
│  HEAP (grows ↑)                       │  small mallocs → brk/sbrk
├───────────────────────────────────────┤
│  BSS  — zero-initialized statics      │
│  Data — non-zero statics              │
│  Text — executable code               │
└───────────────────────────────────────┘
0x0000000000400000  ← program load base
```

### 2.3 Why heap allocation is non-deterministic

1. **Free list search:** glibc malloc uses size bins. Finding a fit is O(1) amortized but with high constant.
2. **Consolidation:** on `free`, adjacent free blocks are merged (coalescing) — O(1) amortized but can cause latency spikes.
3. **Kernel page faults:** first touch of a new page → kernel must zero it → 1–10 µs.
4. **Lock contention:** glibc malloc uses arena locks. Multiple threads → lock competition → unbounded stall.

**Solution for HFT:** pre-allocate everything at startup, use pools/arenas during the hot path.

---

## 3. Smart Pointers — Internals and Costs

### 3.1 unique_ptr — zero overhead

```
sizeof(unique_ptr<T>) == 8   (with default deleter — empty base optimization)
sizeof(unique_ptr<T, D>) == 8 + sizeof(D) if D is non-empty
```

Move is three instructions:
```asm
mov  rax, [src+0]    ; load ptr
mov  [dst+0], rax    ; store ptr
mov  qword [src+0], 0; null out source
```
Total: ~0.3–0.5 ns.

### 3.2 shared_ptr — the hidden cost

```
Memory layout:

┌──────────────────────┐
│ T* ptr      (8 B)    │  → the actual object
│ ctrl_blk*   (8 B)    │  → control block
└──────────────────────┘

Control block:
┌──────────────────────────────┐
│ use_count  : atomic<int> (4B)│  ← PROBLEM
│ weak_count : atomic<int> (4B)│
│ deleter    : function ptr    │
│ allocator  : optional        │
│ T object (if make_shared)    │  ← advantage of make_shared
└──────────────────────────────┘
```

**Copy cost:** `use_count.fetch_add(1, memory_order_relaxed)` emits `lock xadd [mem], 1` on x86. This instruction:
1. Locks the cache line for exclusive access (broadcasts RFO to all cores)
2. Performs the add
3. Releases the lock

On a 2-socket server with 40 cores, this can take **40–150 ns** due to cache coherence traffic. In a market-data handler firing 1M times/sec, that's 40–150 µs/sec wasted on reference counting alone.

### 3.3 make_shared vs new — why it matters

```cpp
// TWO heap allocations, two cache lines:
shared_ptr<Order> p{new Order{id, price}};
//  alloc 1: the Order object
//  alloc 2: the control block (separate malloc call)

// ONE heap allocation, one cache line:
auto p = make_shared<Order>(id, price);
//  alloc 1: [control block | Order] — fused, contiguous
```

With `make_shared`, accessing the ref count and the data land in the same cache line — better locality.

Downside: the object's memory cannot be freed until ALL weak_ptrs are gone (because control block and object share a block).

### 3.4 Alternatives for HFT

| Approach | sizeof | Move cost | Copy cost | Use case |
|---|---|---|---|---|
| by value | sizeof(T) | memcpy | copy ctor | Small objects, hot path |
| unique_ptr | 8 B | ~0.3 ns | ❌ deleted | Single owner, heap obj |
| shared_ptr | 16 B | ~1 ns | ~50–150 ns | Shared ownership, off hot path |
| intrusive_ptr | 8 B | ~0.3 ns | ~5 ns | Hot path shared ownership |
| raw T* (non-owning) | 8 B | ~0.3 ns | ~0.3 ns | Observer, must outlive owner |

**intrusive_ptr:** ref count lives inside T (you control it). No control block. Single pointer. Used in Boost.Asio, some HFT frameworks.

---

## 4. Custom Allocators — The PMR Model

### 4.1 Why PMR (C++17)?

The old template allocator (`std::vector<T, MyAlloc<T>>`) had a fatal flaw: the allocator type was baked into the container type. You couldn't put `vector<int, ArenaAlloc>` into a function expecting `vector<int, DefaultAlloc>`.

PMR (Polymorphic Memory Resource) solves this with a virtual interface:

```cpp
class memory_resource {
public:
    void* allocate(size_t bytes, size_t alignment);
    void  deallocate(void* p, size_t bytes, size_t alignment);
    bool  is_equal(const memory_resource&) const noexcept;
};
```

`std::pmr::vector<T>` is a type alias for `std::vector<T, std::pmr::polymorphic_allocator<T>>`. You can pass it anywhere `vector<T>` is expected by selecting the right memory resource at runtime.

### 4.2 monotonic_buffer_resource — the bump pointer

```
Initial state:
  buffer:  [▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒]
  current: ^ (start)

After 3 allocations:
  buffer:  [Order1|Order2|Order3|▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒]
  current:                       ^

Code:
  allocate(n) {
      ptr = align_up(current_, alignment);
      current_ = ptr + n;
      return ptr;              // O(1), no lock, no search
  }
  deallocate(p, n) { }         // NO-OP. Individual frees are illegal.
  release() { current_ = buf_; } // Reset entire arena in one shot
```

**Latency:** ~1–3 ns per allocation (just pointer arithmetic + alignment rounding).

**Trade-off:** you can't free individual objects. Use when objects have the same lifetime (e.g., all orders in one market-data snapshot).

### 4.3 unsynchronized_pool_resource — the slab

Maintains a free list per size class (e.g., 16, 32, 48, 64, 128, … bytes). Allocation = pop from list head. Deallocation = push to list head. O(1), no lock (unsynchronized version).

Falls back to upstream `memory_resource` (typically `monotonic_buffer_resource` or `new_delete_resource`) when the list is empty.

### 4.4 Production usage pattern

```cpp
// Thread-local message arena — reset each tick
thread_local char arena_buf[1 << 20];  // 1 MB per thread
thread_local std::pmr::monotonic_buffer_resource tick_arena{
    arena_buf, sizeof(arena_buf)
};

void on_tick() {
    // All these allocations are O(1) bump-pointer, zero syscalls:
    std::pmr::vector<Order>  orders{&tick_arena};
    std::pmr::vector<Quote>  quotes{&tick_arena};
    
    decode_feed(orders, quotes);   // allocates from arena
    run_strategy(orders, quotes);
    
    tick_arena.release();          // O(1) reset — all memory "freed"
    // ↑ no destructors called implicitly — must be called manually
    //   OR use objects whose destructors are trivial
}
```

---

## 5. Memory Alignment — Deep Dive

### 5.1 Why alignment matters to the CPU

x86-64 can load unaligned data but with a penalty:
- **Same cache line:** 0–1 extra cycle
- **Crossing a cache-line boundary:** 1 extra cache miss (64 extra ns)
- **Crossing a page boundary:** potential TLB miss

For SIMD instructions (SSE2, AVX2, AVX-512), unaligned loads (`vmovdqu`) are slower than aligned loads (`vmovdqa`). On older microarchitectures, aligned was required (SIGBUS on ARM if violated).

### 5.2 Struct layout rules (standard-layout types)

1. `alignof(T)` = the alignment requirement of T.
2. `sizeof(T)` is always a multiple of `alignof(T)`.
3. First member is at offset 0 (no leading padding).
4. Each member is at the next address divisible by `alignof(member)`.
5. Trailing padding is added to make `sizeof(T)` a multiple of the max member alignment.

```cpp
// Bad layout — 16 bytes, 7 wasted:
struct Bad {
    char   flag;     // offset 0,  size 1, align 1
    // [3 bytes padding]
    float  price;    // offset 4,  size 4, align 4
    char   side;     // offset 8,  size 1, align 1
    // [3 bytes padding]  ← trailing pad so sizeof % 4 == 0
};
static_assert(sizeof(Bad) == 12);

// Good layout — 8 bytes, 1 wasted:
struct Good {
    float  price;    // offset 0,  size 4, align 4
    char   flag;     // offset 4,  size 1, align 1
    char   side;     // offset 5,  size 1, align 1
    // [2 bytes padding]
};
static_assert(sizeof(Good) == 8);

// Optimal — 6 bytes, 0 wasted (if you pack):
#pragma pack(push, 1)
struct Packed { float price; char flag; char side; };
#pragma pack(pop)
static_assert(sizeof(Packed) == 6);
// BUT: unaligned access — NOT recommended in hot paths
```

### 5.3 Cache-line alignment idiom

```cpp
// Prevent false sharing: each object on its own cache line
struct alignas(std::hardware_destructive_interference_size) ThreadCounter {
    std::atomic<uint64_t> value{0};
};

// In an array, adjacent counters are on different lines:
std::array<ThreadCounter, 8> counters;
// Threads 0..7 each own counters[i] — zero coherence traffic
```

### 5.4 HFT Order struct design

Target: fit hot fields in first 32 bytes (half cache line), cold fields in second 32 bytes.

```cpp
struct alignas(64) Order {
    // HOT: 32 bytes — read every tick
    int64_t  price;         // 8 B  offset 0
    uint32_t qty;           // 4 B  offset 8
    uint32_t order_id;      // 4 B  offset 12
    uint64_t symbol_hash;   // 8 B  offset 16
    uint32_t side;          // 4 B  offset 24
    uint32_t flags;         // 4 B  offset 28
    // COLD: 32 bytes — written at order creation/cancel
    uint64_t timestamp_ns;  // 8 B  offset 32
    uint64_t sequence_num;  // 8 B  offset 40
    char     symbol[12];    // 12 B offset 48
    uint32_t venue_id;      // 4 B  offset 60
};
static_assert(sizeof(Order) == 64);
static_assert(alignof(Order) == 64);
```

Reading the order book prices never loads the cold fields → half the cache pressure.

---

## 6. Placement New — Object Lifecycle Control

### 6.1 Separating storage from lifetime

Standard `new T(args)` does two things:
1. Allocates `sizeof(T)` bytes (`operator new`)
2. Constructs T in those bytes (constructor call)

Placement new does **only step 2** in memory you already have:

```cpp
void* storage = my_pool.allocate(sizeof(Order), alignof(Order));
Order* p = new (storage) Order{id, price, qty};  // step 2 only
```

To destroy without freeing:
```cpp
p->~Order();  // step 2 reversed — destructor only
my_pool.deallocate(storage, sizeof(Order));  // step 1 reversed
```

### 6.2 Ring buffer with placement new

```cpp
template <typename T, size_t N>
class RingBuffer {
    alignas(T) unsigned char storage_[sizeof(T) * N];
    size_t head_{0}, tail_{0};
    size_t mask_{N - 1};

public:
    template <typename... Args>
    void push(Args&&... args) {
        new (slot(head_)) T(std::forward<Args>(args)...);  // construct in-place
        head_ = (head_ + 1) & mask_;
    }

    T pop() {
        T val = std::move(*reinterpret_cast<T*>(slot(tail_)));
        reinterpret_cast<T*>(slot(tail_))->~T();           // destroy in-place
        tail_ = (tail_ + 1) & mask_;
        return val;
    }

private:
    void* slot(size_t idx) { return storage_ + sizeof(T) * idx; }
};
```

### 6.3 std::launder — pointer provenance

After placement-new reuses a buffer, the old pointer is poisoned by the standard (the object's identity changed). You must launder:

```cpp
alignas(Order) char buf[sizeof(Order)];

Order* p1 = new (buf) Order{1, 100, 10};
p1->~Order();

Order* p2_wrong = reinterpret_cast<Order*>(buf);  // UB: compiler may cache p1's fields
Order* p2       = std::launder(reinterpret_cast<Order*>(buf));  // Correct

new (buf) Order{2, 200, 20};
// Now p2 (laundered) correctly sees the new object
// p2_wrong might see stale data due to compiler CSE
```

### 6.4 trivially_copyable — when memcpy is legal

```cpp
static_assert(std::is_trivially_copyable_v<Order>);
// Then these are equivalent and the compiler can SIMD-copy them:
Order dst;
std::memcpy(&dst, &src, sizeof(Order));   // legal
// vs:
dst = src;  // also legal, but compiler might not vectorize the copy ctor
```

Trivially copyable = no user-defined copy/move ctors or dtors, and all members are trivially copyable. This is what lets `memcpy`-based ring buffers work correctly.

---

## 7. Memory Pools and Arenas — Production Patterns

### 7.1 Fixed-size pool allocator

```cpp
template <typename T, size_t PoolSize = 4096>
class PoolAllocator {
    union Slot {
        T        obj;
        Slot*    next;
    };

    alignas(Slot) char  storage_[sizeof(Slot) * PoolSize];
    Slot* free_list_{nullptr};
    size_t capacity_{PoolSize};

public:
    PoolAllocator() {
        // Pre-link all slots into free list
        auto* s = reinterpret_cast<Slot*>(storage_);
        for (size_t i = 0; i + 1 < capacity_; ++i)
            s[i].next = &s[i + 1];
        s[capacity_ - 1].next = nullptr;
        free_list_ = s;
    }

    T* allocate() {
        if (!free_list_) throw std::bad_alloc{};
        Slot* slot = free_list_;
        free_list_ = free_list_->next;
        return reinterpret_cast<T*>(slot);   // O(1), zero syscall
    }

    void deallocate(T* p) noexcept {
        auto* slot = reinterpret_cast<Slot*>(p);
        slot->next = free_list_;
        free_list_ = slot;                   // O(1) push to head
    }
};
```

**Throughput:** ~5–10 ns per alloc/dealloc (just two pointer operations vs glibc's 50+ ns).

### 7.2 Arena + destructor tracking

When you need destructors called even with arena allocation:

```cpp
class Arena {
    char* buf_;
    char* cur_;
    size_t cap_;
    std::vector<std::function<void()>> dtors_;  // only for non-trivial types

public:
    template <typename T, typename... Args>
    T* emplace(Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T));
        T* obj = new (mem) T(std::forward<Args>(args)...);
        if constexpr (!std::is_trivially_destructible_v<T>)
            dtors_.push_back([obj]{ obj->~T(); });
        return obj;
    }

    ~Arena() {
        for (auto it = dtors_.rbegin(); it != dtors_.rend(); ++it)
            (*it)();  // run dtors in reverse order
        // free buf_ (or it's stack-allocated)
    }
};
```

### 7.3 Zero-malloc benchmark goal

Instrument your hot path with:
```cpp
static std::atomic<size_t> malloc_count{0};

void* operator new(size_t n) {
    malloc_count.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(n);
}
```

Target: `malloc_count` should be **0** during the hot path after warmup.

---

## 8. Small Buffer Optimization — Full Mechanics

### 8.1 std::string SSO anatomy

**libstdc++ (GCC):**
```cpp
// sizeof(std::string) == 32 on GCC (32-bit size field, 32-bit capacity, 8-byte ptr/buf)
// Actually sizeof == 32 with union hack
// SSO threshold: 15 chars (16 with null terminator fits in 16-byte union member)

union {
    struct {
        char*    ptr;      // pointer to heap buffer
        size_t   length;
        size_t   capacity; // NOT OR'd with flag here
    } large;
    struct {
        char buf[16];      // inline buffer (15 chars + null)
        // last byte encodes length in small mode (15 - len)
    } small;
};
```

**libc++ (Clang):**
```
sizeof(std::string) == 24
SSO threshold: 22 chars
```

**Check at runtime:**
```cpp
auto is_sso = [](const std::string& s) {
    // Hack: if the string data pointer points inside the string object itself
    const char* data = s.data();
    const char* obj  = reinterpret_cast<const char*>(&s);
    return data >= obj && data < obj + sizeof(s);
};
```

### 8.2 std::function — when it goes to heap

```cpp
// Small lambda — fits in SBO buffer (~24 bytes) — NO heap:
auto f1 = std::function<void()>([]{ return; });

// Large capture — overflows SBO — heap allocation:
std::vector<int> big(1000);
auto f2 = std::function<void()>([big]{ return; });  // heap!

// Alternative: use a SmallFunction<F, N> that static_asserts on size:
template <typename Sig, size_t BufSize = 64>
class SmallFunction; // implementation below
```

### 8.3 SmallFunction<Sig, N> — production pattern

```cpp
template <typename R, typename... Args, size_t N>
class SmallFunction<R(Args...), N> {
    alignas(std::max_align_t) char buf_[N];
    R (*invoke_)(void*, Args...){nullptr};
    void (*destroy_)(void*){nullptr};

public:
    template <typename F>
    SmallFunction(F&& f) {
        static_assert(sizeof(F) <= N, "Lambda too large for SmallFunction buffer");
        new (buf_) F(std::forward<F>(f));
        invoke_  = [](void* b, Args... a) -> R {
            return (*reinterpret_cast<F*>(b))(std::forward<Args>(a)...);
        };
        destroy_ = [](void* b) { reinterpret_cast<F*>(b)->~F(); };
    }

    R operator()(Args... args) {
        return invoke_(buf_, std::forward<Args>(args)...);
    }

    ~SmallFunction() { if (destroy_) destroy_(buf_); }
};
```

**Key:** `static_assert(sizeof(F) <= N)` — compile-time guarantee that no heap allocation occurs. If the lambda is too big, **it's a compile error, not a runtime allocation**.

### 8.4 SmallVec<T, N>

```cpp
template <typename T, size_t N>
class SmallVec {
    union {
        alignas(T) char  inl_[sizeof(T) * N];  // inline storage
        struct {
            T*     ptr;
            size_t cap;
        } heap_;
    };
    size_t size_{0};
    bool   on_heap_{false};

public:
    void push_back(T val) {
        if (!on_heap_ && size_ < N) {
            new (inl_ + sizeof(T) * size_++) T(std::move(val));
        } else {
            move_to_heap();
            heap_.ptr[size_++] = std::move(val);
        }
    }
    // ... rest omitted for brevity
};
```

---

## 9. False Sharing — The Invisible Throughput Killer

### 9.1 MESI protocol basics

Modern CPUs maintain cache coherence via the MESI protocol. Each cache line is in one of four states:
- **M** (Modified): this core has the only valid copy, it's dirty
- **E** (Exclusive): this core has the only valid copy, it's clean
- **S** (Shared): multiple cores have a clean copy
- **I** (Invalid): stale, must fetch from elsewhere

When CPU A wants to **write** to a line that CPU B holds:
1. A sends an **RFO** (Request For Ownership) message on the QPI/UPI interconnect
2. B must **invalidate** its copy and acknowledge
3. A receives the line in M state and writes

This round-trip is **40–400 ns** depending on the CPU socket topology. False sharing triggers this for every write to any variable on a shared line.

### 9.2 Benchmark: false sharing vs padded

```
Without padding:
  Thread A writes counter_a (on same line as counter_b)
  Thread B writes counter_b
  → RFO ping-pong every write
  → ~2–5 M ops/sec total (instead of ~3 B/sec each)

With padding:
  struct alignas(64) Counter { atomic<uint64_t> val; char _pad[56]; };
  → Each counter on its own line
  → No coherence traffic
  → ~1.5–2 B ops/sec each (near theoretical max)
  → ~200–500× speedup
```

### 9.3 hardware_constructive_interference_size

C++17 also defines `hardware_constructive_interference_size` — the maximum size for objects to **share** a cache line and benefit from spatial locality. Typically also 64 bytes.

Pattern: pack frequently co-accessed fields within `hardware_constructive_interference_size` bytes. Separate concurrently-written fields by at least `hardware_destructive_interference_size` bytes.

### 9.4 Prefetch hints

```cpp
// Prefetch for read, low temporal locality (stream through data once):
__builtin_prefetch(ptr + 8, 0, 0);

// Prefetch for read, high temporal locality (will reuse soon):
__builtin_prefetch(ptr + 8, 0, 3);

// Prefetch for write:
__builtin_prefetch(ptr + 8, 1, 3);
```

Prefetch `k` iterations ahead where `k = cache_miss_latency / compute_time_per_iter`. For L2 miss (~50 ns) and 1 ns/iter, prefetch 50 iterations ahead.

### 9.5 NUMA-aware allocation

On a 2-socket server, memory latency to the remote socket is ~140 ns vs ~70 ns local. For an order-book thread pinned to socket 0, always allocate order-book memory on socket 0:

```cpp
// Linux NUMA API:
#include <numa.h>
void* local_buf = numa_alloc_onnode(size, numa_node_of_cpu(sched_getcpu()));

// Alternative: mmap + mbind:
void* p = mmap(nullptr, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
unsigned long nodemask = 1UL << target_node;
mbind(p, size, MPOL_BIND, &nodemask, max_node + 1, 0);
```

---

## 10. RAII — The C++ Resource Contract

### 10.1 Why RAII beats try/finally

Java:
```java
FileInputStream f = new FileInputStream("x");
try {
    f.read();
} finally {
    f.close();  // works but: what if constructor of next_thing throws between open and try?
}
```

C++:
```cpp
std::ifstream f{"x"};  // RAII — destructor guaranteed on ALL exit paths
f.read();
// ← destructor runs here: return, exception, early return, end of scope — ALL covered
// No "finally" block. No way to forget.
```

The guarantee: C++ destructors run for all **fully constructed** objects when a scope exits, regardless of how. An exception in the middle? Destructors run. Early return? Destructors run. `std::abort`? No — you opted out.

### 10.2 Rule of Five — when you need it

```cpp
class Buffer {
    char* data_;
    size_t size_;

public:
    Buffer(size_t n) : data_{new char[n]}, size_{n} {}

    // If you write a destructor, you MUST also handle:
    ~Buffer() { delete[] data_; }                      // ← you wrote this

    // 1. Copy constructor — do deep copy
    Buffer(const Buffer& o) : data_{new char[o.size_]}, size_{o.size_} {
        std::memcpy(data_, o.data_, size_);
    }

    // 2. Copy assignment — copy-and-swap idiom
    Buffer& operator=(Buffer o) noexcept {  // o is a copy
        swap(*this, o);                      // swap, then o's dtor cleans old
        return *this;
    }

    // 3. Move constructor — steal the resource
    Buffer(Buffer&& o) noexcept : data_{o.data_}, size_{o.size_} {
        o.data_ = nullptr; o.size_ = 0;
    }

    // 4. Move assignment — swap is simplest
    Buffer& operator=(Buffer&& o) noexcept {
        swap(*this, o);
        return *this;
    }

    friend void swap(Buffer& a, Buffer& b) noexcept {
        std::swap(a.data_, b.data_);
        std::swap(a.size_, b.size_);
    }
};
```

### 10.3 HFT RAII patterns

**ScopedTimer:**
```cpp
struct ScopedTimer {
    const char* label;
    uint64_t    start;
    ScopedTimer(const char* l) noexcept : label{l}, start{__rdtsc()} {}
    ~ScopedTimer() noexcept {
        uint64_t elapsed = __rdtsc() - start;
        // lock-free histogram write — zero heap
        g_latency_hist.record(label, elapsed);
    }
};
```

**Lock-free memory pin (mlockall):**
```cpp
struct MemoryLock {
    MemoryLock()  { if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0) throw ...; }
    ~MemoryLock() { munlockall(); }
};
// Prevents page faults during market hours — deterministic latency
```

**UDP socket:**
```cpp
class UdpSocket {
    int fd_;
public:
    UdpSocket(uint16_t port) {
        fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) throw std::system_error{errno, std::generic_category()};
        // bind ... (if bind throws, fd_ is leaked WITHOUT RAII fd wrapper below)
        // Better: wrap fd_ in a RAII FdGuard first, then bind
    }
    ~UdpSocket() noexcept { close(fd_); }
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& o) noexcept : fd_{o.fd_} { o.fd_ = -1; }
};
```

---

## 11. Putting It All Together — Zero-Allocation Market Data Pipeline

```
Startup (once):
  ┌─────────────────────────────────────────────┐
  │  1. mlock all memory (prevent page faults)   │
  │  2. Preallocate 1M Order pool (PoolAlloc)    │
  │  3. Preallocate 64KB per-thread arena        │
  │  4. Pin threads to CPU cores (sched_setaff.) │
  │  5. Pre-fault all pages (memset to 0)        │
  └─────────────────────────────────────────────┘

Hot path per tick (~100 ns budget):
  ┌────────────────────────────────────────────────────────────────┐
  │ recv UDP packet → decode into arena (bump ptr, 0 malloc)       │
  │ look up order in hash map (stack-allocated key, no alloc)      │
  │ update order-book price level (array indexed by tick, no alloc)│
  │ fire callback via SmallFunction<> (SBO buffer, no alloc)       │
  │ reset tick arena (one pointer reset, O(1))                     │
  └────────────────────────────────────────────────────────────────┘

Malloc count on hot path: 0
Page faults on hot path: 0
Lock syscalls on hot path: 0
```

This is the memory management chapter condensed into a discipline: **allocate everything upfront, reuse in the hot path, never call the OS when latency matters**.

---

## 12. Cheat Sheet

| Technique | Latency | Use when |
|---|---|---|
| Stack allocation | ~0.3 ns | Small, short-lived objects |
| Bump-pointer arena | ~1–3 ns | Same-lifetime batch, reset at once |
| Pool allocator | ~5–10 ns | Fixed-size objects, O(1) alloc+dealloc |
| tcmalloc hot path | ~15 ns | General purpose, off hot path |
| glibc malloc | ~50 ns | Off hot path, convenience |
| unique_ptr move | ~0.3 ns | Single owner, heap object |
| shared_ptr copy | ~50–150 ns | Shared ownership, off hot path |
| SSO string | ~0 heap | Symbols ≤15 chars |
| SmallFunction | ~0 heap | Callbacks with small captures |
| false-shared counter | ~2-5 M/s | ← AVOID: add `alignas(64)` padding |
| padded counter | ~1.5 B/s | Per-thread counters, separate lines |
| NUMA-local alloc | ~70 ns RAM | Memory-intensive, multi-socket |
| NUMA-remote alloc | ~140 ns RAM | Avoid for hot data |
