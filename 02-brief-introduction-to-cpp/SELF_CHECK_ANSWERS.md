# Self-Check Questions: Model Answers (Study Guide)

> **Instructions:** Answer these questions from memory BEFORE reading the answers below.
> 
> Score yourself: 7-8 correct = ready for Level 2 | 5-6 correct = re-read NOTES.md | <5 correct = spend more time on BFS layers

---

## Question 1: C vs C++ Hamlet Counter — Assembly Comparison

**The Question:**
> Write the C and C++ versions of counting "Shakespeare" in a string. Which produces fewer machine instructions?

### Model Answer:

**C version:**
```c
int num_shakespeare(const char* text) {
    int count = 0;
    for (int i = 0; text[i] != '\0'; i++) {
        if (strcmp(&text[i], "Shakespeare") == 0) {
            count++;
        }
    }
    return count;
}
```

**C++ version:**
```cpp
#include <string>
#include <algorithm>

int num_shakespeare(const std::string& text) {
    return std::count_if(text.begin(), text.end(), 
                         [](char c) { return c == 'S'; });
}
```

(Or more directly, using `std::string::find()` in a loop — similar assembly)

**Assembly comparison:**
- **Both compile to nearly identical machine code** when optimized with `-O2`
- The C++ version might emit **0–2 extra instructions** for:
  - `std::string` member access (length, pointer)
  - Iterator comparisons
- Modern compilers fully inline lambdas and standard library functions
- **Winner:** Tie (zero-cost abstraction achieved)

**Key insight:** The C version forces you to manage:
- Character-by-character scanning
- Null terminator checking
- `strcmp` call overhead

The C++ version lets you *express intent* (`std::count`) while the compiler does the rest.

---

## Question 2: Memory Layout — std::vector<Car> vs ArrayList<Car>

**The Question:**
> Draw the memory layout of `std::vector<Car> cars(7)` in C++ and equivalent in Java. Why does C++ win?

### Model Answer:

**C++ memory layout:**
```
Stack:
┌──────────────────────┐
│ cars: vector         │
│ ├─ data_: ptr        │ ──┐
│ ├─ size_: 7          │   │
│ └─ cap_: 7           │   │
└──────────────────────┘   │
                           │
Heap (contiguous):         │
┌──────────────────────────────────────┐ <─┘
│ Car0 │ Car1 │ Car2 │ ... │ Car6      │
│ (56B) × 7 = 392 bytes contiguous     │
└──────────────────────────────────────┘
Total allocations: 1
```

**Java memory layout:**
```
Stack:
┌──────────────────────┐
│ cars: ArrayList      │
│ ├─ elements_: ptr    │ ──┐
│ ├─ size_: 7          │   │
│ └─ cap_: 10          │   │
└──────────────────────┘   │
                           │
Heap (array of references):│
┌────────────────────────┐ <─┘
│ Car0_ptr │ Car1_ptr │ ...│
└────────────────────────┘
       ↓        ↓
     ┌──────┐ ┌──────┐ ... ┌──────┐
     │Car0  │ │Car1  │     │Car6  │
     │(56B) │ │(56B) │     │(56B) │
     └──────┘ └──────┘     └──────┘

Total allocations: 8 (1 array + 7 objects)
Pointer chasing: 2 hops per access
```

**Why C++ wins:**

| Factor | C++ | Java |
|--------|-----|------|
| **Allocations** | 1 | 8 |
| **Memory per element** | 56B | 8B (ref) + 56B (object) = 64B |
| **Cache prefetch** | Linear ✅ | Indirection ❌ |
| **Access latency** | 1 hop | 2 hops |
| **Iteration speed** | 5–20× faster | Baseline |

**For an HFT order book with 10,000 orders:**
- C++: contiguous buffer, cache prefetch hides latency
- Java: 10,000 pointer chases, L3 misses on each one = milliseconds of latency

---

## Question 3: Bagel Move Semantics — Why `:toppings_(std::move(ts))`?

**The Question:**
> Why does `Bagel(std::set<std::string> ts)` end with `: toppings_(std::move(ts))` rather than `: toppings_(ts)`?

### Model Answer:

**If you use `: toppings_(ts)`:**
```cpp
Bagel::Bagel(std::set<std::string> ts) : toppings_(ts) {
    // Step 1: parameter ts is constructed (caller's copy)
    // Step 2: toppings_ = ts (copy constructor for std::set)
    // Step 3: ts is destroyed (set destructor)
    // Result: TWO std::set objects constructed and destroyed
    // Cost: copy + destroy = slow
}
```

**If you use `: toppings_(std::move(ts))`:**
```cpp
Bagel::Bagel(std::set<std::string> ts) : toppings_(std::move(ts)) {
    // Step 1: parameter ts is constructed (caller's copy)
    // Step 2: toppings_ = move(ts) (move constructor for std::set)
    // Step 3: ts is destroyed (now empty, no work)
    // Result: Move instead of copy
    // Cost: move constructor only = fast
}
```

**Why this matters:**
- The move constructor for `std::set` just transfers ownership of the internal tree
- No deep copy of individual strings happens
- On destruction of ts, the destructor does minimal work (set is now empty)
- **Savings:** Entire tree copy avoided

**Generic rule (the "sink" idiom):**
```cpp
// If you're taking ownership of a parameter, use move:
class MyClass {
public:
    MyClass(std::vector<int> data) : data_{std::move(data)} {}
    MyClass(std::string name) : name_{std::move(name)} {}
    
private:
    std::vector<int> data_;
    std::string name_;
};
```

---

## Question 4: Const Overloading — When Each Overload is Selected

**The Question:**
> Given `class Team { Person leader_; public: auto& leader() const; auto& leader(); };` — when is each overload selected?

### Model Answer:

```cpp
class Team {
    Person leader_;
    
public:
    // Overload 1: const version (selected when *this is const)
    const Person& leader() const {
        return leader_;  // returns const reference
    }
    
    // Overload 2: mutable version (selected when *this is mutable)
    Person& leader() {
        return leader_;  // returns mutable reference
    }
};
```

**Selection rule:**

| Context | `*this` type | Overload selected | Result |
|---------|-------------|-------------------|--------|
| `const Team& team = ...;` | `const Team*` | `const` version | Can read only |
| `Team team = ...;` | `Team*` | Non-const version | Can read & mutate |
| `team.leader().set_age(50)` | `Team*` | Non-const version | Allowed |
| `const_team.leader().set_age(50)` | `const Team*` | `const` version | **Compile error** |

**Real example:**
```cpp
Team team;
const Team& cteam = team;

team.leader().set_age(50);           // ✅ Calls non-const version
cteam.leader().set_age(50);          // ❌ Compile error: const version returns const Person&

const Person& p = cteam.leader();    // ✅ const version, returns const Person&
```

**Why this matters:**
- Const-correctness at compile time
- Compiler prevents accidental mutation of const objects
- Type safety without annotations

---

## Question 5: Java to C++ Nullability Contract

**The Question:**
> Convert this Java signature to C++ that documents its nullability contract: `float getVolume(Sphere s)`. Make it non-nullable.

### Model Answer:

**Java version (ambiguous):**
```java
float getVolume(Sphere s)  // Could s be null? Compiler doesn't enforce.
```

**C++ version (explicit):**
```cpp
// Option 1: Non-nullable reference (PREFERRED)
float get_volume(const Sphere& s) {
    return 4.0f/3.0f * M_PI * s.radius() * s.radius() * s.radius();
}

// Option 2: Non-nullable pointer (if you prefer pointers)
float get_volume(const Sphere* s) {
    // Contract: s must not be nullptr
    // But compiler doesn't enforce; must be documented
    return 4.0f/3.0f * M_PI * s->radius() * s->radius() * s->radius();
}

// Option 3: Nullable (if null is allowed)
float get_volume(const Sphere* s) {
    if (!s) return 0.0f;  // Callers see the null check
    return 4.0f/3.0f * M_PI * s->radius() * s->radius() * s->radius();
}

// Option 4: Optional (explicit null-or-value)
#include <optional>
float get_volume(std::optional<std::reference_wrapper<const Sphere>> s) {
    if (!s) return 0.0f;
    return 4.0f/3.0f * M_PI * s->get().radius() * s->get().radius() * s->get().radius();
}
```

**Nullability contracts:**
| Signature | Null allowed? | Enforced? |
|-----------|--------------|-----------|
| `const Sphere&` | **NO** | ✅ Compiler |
| `const Sphere*` | **YES** | ❌ Documentation only |
| `std::optional<Sphere>` | **YES** | ✅ Compiler |

**Best practice:** Default to `const T&` for non-null, `T*` for nullable.

---

## Question 6: Strong Exception Guarantee & Copy-and-Swap

**The Question:**
> What's the *strong* exception guarantee, and how does copy-and-swap achieve it?

### Model Answer:

**Exception guarantees (3 levels):**

| Level | Guarantee | Example |
|-------|-----------|---------|
| **Basic** | No leaks; object in valid state (possibly different) | Naive assignment |
| **Strong** | All-or-nothing: if op throws, state unchanged | Copy-and-swap |
| **No-throw** | Cannot throw | Destructors, `swap` |

**Strong exception guarantee definition:**
> If an operation throws an exception, the program state is completely unchanged, as if the operation never started. (Commit-or-rollback semantics.)

**Without copy-and-swap (BASIC guarantee):**
```cpp
class Tree {
    std::vector<int> leafs_;
    std::vector<int> branches_;
    
public:
    Tree& operator=(const Tree& other) {
        leafs_ = other.leafs_;        // THROW POINT 1
        branches_ = other.branches_;  // THROW POINT 2
        return *this;
    }
};

// If THROW POINT 1 succeeds but POINT 2 throws:
// leafs_ is NEW, branches_ is OLD → inconsistent state ❌
```

**With copy-and-swap (STRONG guarantee):**
```cpp
class Tree {
    std::vector<int> leafs_;
    std::vector<int> branches_;
    
public:
    Tree& operator=(const Tree& other) {
        // All throwing operations on COPIES, not *this
        auto leafs    = other.leafs_;      // THROW POINT (local, safe)
        auto branches = other.branches_;   // THROW POINT (local, safe)
        
        // Swaps are no-throw primitives
        std::swap(leafs_, leafs);         // NO-THROW
        std::swap(branches_, branches);   // NO-THROW
        
        return *this;
    }
    // If any THROW POINT throws, local copies are destroyed,
    // *this is completely untouched → strong guarantee ✅
};
```

**Why it works:**
1. All throwing operations happen on local **copies**
2. The original `*this` is read-only until...
3. Only no-throw `swap` operations touch `*this`
4. If anything throws before the swaps, nothing changed

---

## Question 7: Lock Guard vs Manual Lock/Unlock

**The Question:**
> Why is `std::lock_guard` preferred over manual `m.lock()` / `m.unlock()` calls?

### Model Answer:

**Manual locking (PROBLEMATIC):**
```cpp
void process(std::mutex& m) {
    m.lock();                   // Acquire
    
    if (condition) return;      // ❌ LEAK: forgot to unlock!
    
    risky_operation();          // ❌ LEAK: if this throws!
    
    m.unlock();                 // Never reached in error paths
}
```

**With lock_guard (SAFE):**
```cpp
void process(std::mutex& m) {
    std::lock_guard<std::mutex> guard(m);  // Acquire in constructor
    
    if (condition) return;      // ✅ guard destructor unlocks
    
    risky_operation();          // ✅ If throws, guard destructor unlocks
    
}  // ✅ guard destructor unlocks here
```

**Why lock_guard is better:**
1. **RAII principle:** Resource (lock) is tied to object lifetime
2. **Exception safety:** Destructor *always* runs (even on exception)
3. **Deterministic:** Lock is released at predictable time
4. **No leaks:** Impossible to forget `unlock()`

**Why this matters in HFT:**
- Every lock held longer than necessary = contention = jitter
- Manual unlock is error-prone; RAII guarantees precision
- In latency-critical code, non-determinism is death

**Scope-based release:**
```cpp
{
    std::lock_guard<std::mutex> guard(m);
    // Lock held here
    critical_section();
}  // Lock released HERE, not later
   // Other threads can proceed
```

---

## Question 8: Why Avoid `shared_ptr` in HFT Hot Path

**The Question:**
> Name one concrete HFT reason to avoid `std::shared_ptr` in the hot path.

### Model Answer:

**The atomic operations problem:**

```cpp
// Every copy/move of shared_ptr involves atomics:
std::shared_ptr<Order> order_ptr = ...;
auto copy = order_ptr;  // ← Atomic operations inside

// What happens inside:
// 1. atomic_increment(&control_block->ref_count)   // ← cacheline bounce
// 2. memory_fence()                                 // ← wait for coherence
// 3. atomic_decrement(&old_ref_count)               // ← another bounce
```

**Concrete HFT scenario:**
```
Hot loop (order book iteration):
  for (auto& order : orders) {  // 1M iterations
      auto ptr = shared_ptr_to_engine;  // atomic ops
      process(*ptr);
  }

Cost analysis:
- Non-atomic operation:  ~1 nanosecond/iteration
- shared_ptr copy:       ~50 nanoseconds/iteration (atomics, fence)
- Difference:            49 ns × 1M = 49 milliseconds

That's your entire latency budget for a microsecond-scale HFT system.
```

**Contention on multi-core:**
```
Core 1:  shared_ptr copy → atomic_increment
         ↑
         └─── cacheline BOUNCE ───┐
                                  ↓
Core 2:  shared_ptr copy → atomic_increment

Both cores competing for the same cacheline that holds ref_count.
Result: ~100-200 ns latency per operation (cache coherency stalls)
```

**Solutions:**
1. **Use `unique_ptr`** — No atomics, just pointer swap
2. **Use by-value** — No pointers at all
3. **Use raw pointers** — If lifetime managed elsewhere
4. **Move instead of copy** — `std::move` is no-cost

**Concrete example (HFT order matching):**
```cpp
// ❌ BAD: shared_ptr in hot loop
struct Order {
    std::shared_ptr<Trader> trader_;  // Atomic ops every access
    Price price_;
    Quantity qty_;
};

// ✅ GOOD: pointer or by-value
struct Order {
    Trader* trader_;  // Raw pointer, lifetime managed by OrderBook
    Price price_;
    Quantity qty_;
};

// ✅ BETTER: by-value if Trader is small
struct Order {
    TraderId trader_id_;  // Just an int
    Price price_;
    Quantity qty_;
};
```

---

## Self-Assessment Scoring

| Score | What it means | Next step |
|-------|--------------|-----------|
| 8/8 ✅ | Perfect | Move to Level 2 (hands-on) |
| 7/8 ✅ | Excellent | Move to Level 2, review 1 question |
| 6/8 ✅ | Good | Move to Level 2, review 2 questions |
| 5/8 ⚠️ | Decent | Spend 1 more hour on NOTES.md, then Level 2 |
| <5 ❌ | Needs work | Re-read NOTES.md BFS Layers 0-2 completely |

---

## Tips for Retention

1. **Diagram the memory:** For each answer, draw the stack/heap layout
2. **Teach someone:** Explain each answer to an imaginary colleague
3. **Code along:** Implement each concept in a test file
4. **Connect the dots:** Notice how all 8 answers relate to the 4 pillars (zero-cost, value semantics, const-correctness, RAII)

Good luck! 🚀
