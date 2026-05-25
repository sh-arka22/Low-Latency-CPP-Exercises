# Chapter 2: A Brief Introduction to C++

## 📖 Topics Covered

- Why C++?
- Important Features of C++
- C++ Compared With Other Languages
- Non-performance-Related C++ Language Features

## 📝 Key Concepts & Revision Notes

In this chapter, we explored the four fundamental pillars that make C++ unique, especially for High-Frequency Trading (HFT) and low-latency systems:

### 1. Zero-Cost Abstractions
You don't pay for what you don't use, and what you use costs no more than hand-coding it. High-level constructs (like `std::count` with iterators) compile down to the same machine code as manual C-style `for` loops and raw pointers. This gives you the readability of a high-level domain model with the performance of raw metal.

### 2. Memory Layout & Cache Locality
C++ provides ultimate control over where data lives (Stack vs. Heap) and how it's laid out (Contiguous vs. Indirected).
* **Contiguous Memory (`std::vector<T>`)**: Objects are packed tightly next to each other. Iterating through them results in linear cache prefetching (cache hits), making it extremely fast.
* **Indirection (`std::vector<std::unique_ptr<T>>` or Java objects)**: Objects are scattered across the heap. Iterating requires pointer chasing, leading to expensive cache misses (~100ns penalty per miss).

### 3. Value Semantics & Object Ownership
Assignment in C++ defaults to copying values, ensuring independent state, unlike Java where references are shared.
* **HFT Best Practice**: **Pass and store by value** is the absolute winner for hot paths. Embedding objects (e.g., storing `Engine` directly inside `Boat`) guarantees zero heap allocations, zero pointer chasing, and fits data neatly into a single cache line.
* Use `std::unique_ptr` only when polymorphism is strictly necessary. Never use `std::shared_ptr` on a hot path due to atomic reference counting overhead.

### 4. Const Correctness & Strict Interfaces
* **`const`**: A compile-time guarantee that a function or reference will not mutate state. It acts as enforced documentation and prevents accidental aliasing/data-race bugs.
* **Strict Interfaces**: Prevent silent aliasing by explicitly deleting the default copy constructor/assignment operators (`= delete`) and forcing explicit `clone()` methods for deep copies.

### 5. Deterministic Destruction (RAII) & Exception Safety
Resource Acquisition Is Initialization (RAII) binds a resource's lifetime to an object's scope. When the object goes out of scope, the destructor runs deterministically, releasing the resource (e.g., unlocking a mutex, freeing memory) regardless of how the scope was exited (normal return, early exit, or exception). This predictability is crucial for avoiding GC pauses in HFT.

## 💻 Code Examples

See the [`code/`](./code/) directory for implementations, particularly:
* `strict_interfaces_by_value.cpp` for the optimal HFT memory layout.
* `heap_allocations.cpp` for stack vs heap comparisons.
* `copy_and_swap.cpp` for strong exception safety guarantees.
