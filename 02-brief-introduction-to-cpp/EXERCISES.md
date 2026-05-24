# Chapter 2 Synthesis Exercises

## L3.1 OrderBook Row Design

```cpp
#include <vector>
#include <string>

struct Order {
    double price;
    int quantity;
    // ...
};

class OrderBookRow {
public:
    // Move-only semantics (non-copyable)
    OrderBookRow(const OrderBookRow&) = delete;
    OrderBookRow& operator=(const OrderBookRow&) = delete;

    OrderBookRow(OrderBookRow&&) noexcept = default;
    OrderBookRow& operator=(OrderBookRow&&) noexcept = default;

    explicit OrderBookRow(std::string symbol) 
        : symbol_(std::move(symbol)) {}

    // Const-correct views
    const std::string& symbol() const { return symbol_; }
    const std::vector<Order>& orders() const { return orders_; }
    
    // RAII-managed memory via std::vector (deterministic destruction)
    void add_order(Order order) {
        orders_.push_back(std::move(order));
    }

private:
    std::string symbol_;
    std::vector<Order> orders_;
};
```

## L3.2 Code Review Checklist for Low-Latency C++
1. **Zero-cost Abstractions:** Are templates, lambdas, or `constexpr` used where runtime polymorphism was written?
2. **Memory Layout:** Are contiguous containers (`std::vector`, `std::array`) favored over node-based/indirect ones (lists, trees, `unique_ptr`) for cache locality?
3. **Value Semantics & Ownership:** Is pass-by-value + `std::move` used for sinks? Are `shared_ptr`s eliminated from the hot path?
4. **Const-Correctness:** Are all non-mutating methods and logical observer arguments marked `const`?
5. **Reference vs Pointer:** Are `T*` used only for strictly nullable arguments and `T&` for everything else?
6. **Strict Interfaces:** Are copy constructors `= delete`d for classes that shouldn't be copied silently (like RAII wrappers)?
7. **Exception Guarantees:** Do mutations offer at least the strong exception guarantee (e.g. via copy-and-swap) or `noexcept`? 
8. **RAII/Destruction:** Are raw resource cleanups absent in favor of RAII guards?

## L3.3 Why `std::vector<Order>` > `std::vector<std::shared_ptr<Order>>`
*   **Cache Locality:** `std::vector<Order>` allocates items contiguously. The hardware prefetcher loads multiple orders per cache line seamlessly. `vector<shared_ptr<Order>>` is a pointer chase—every element read involves loading the pointer, then jumping to a random heap location, resulting in L1/L2 cache misses.
*   **Allocations:** 1 allocation for `vector<Order>` vs (1 + N) allocations for pointers, introducing heavy overhead.
*   **Atomic Ops:** `shared_ptr` copies trigger atomic increments/decrements. These interlocked CPU instructions cripple performance on the hot path compared to plain values.
