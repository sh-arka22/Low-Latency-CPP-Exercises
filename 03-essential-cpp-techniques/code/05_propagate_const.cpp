/**
 * Chapter 2 — Essential C++ Techniques
 * Topic 5: propagate_const — Const Through Pointer Indirection
 *
 * ── FIRST PRINCIPLES ──────────────────────────────────────────────────────────
 *
 * THE PROBLEM:
 *   `const` on an object means its DIRECT members can't be modified.
 *   But a pointer MEMBER that's const means the POINTER itself can't
 *   change — not what it points to:
 *
 *       struct Foo { int* ptr_; };
 *       const Foo f = ...;
 *       *f.ptr_ = 42;  // COMPILES! const doesn't protect the pointed-to int
 *
 * THE SOLUTION: std::experimental::propagate_const<T>
 *   A wrapper that propagates const-ness through the pointer:
 *   - When the wrapper lives in a const object, operator* and operator->
 *     return const T& and const T* respectively.
 *   - When in a non-const object, returns T& and T*.
 *
 * WHY IT MATTERS IN HFT:
 *   You often have a class that holds a pointer to an implementation
 *   (Pimpl idiom). Without propagate_const, a const method could modify
 *   the implementation through the pointer — silently breaking the const contract.
 *   propagate_const enforces physical const-ness, not just logical.
 *
 * NOTE: std::experimental::propagate_const (C++17 TS) — not yet in the standard
 *       but available in libstdc++ and libc++. Production alternative: write your own.
 */

#include <iostream>
#include <memory>

// ─── SECTION 1: The problem without propagate_const ──────────────────────────

struct InnerData {
    int value = 0;
};

class WithRawPtr {
public:
    explicit WithRawPtr(int v) : data_(new InnerData{v}) {}
    ~WithRawPtr() { delete data_; }

    // const method — but can still modify *data_!
    void set_value(int v) const {
        data_->value = v;  // COMPILES — const applies to data_ (the pointer), not *data_
    }

    int get_value() const { return data_->value; }

private:
    InnerData* data_;  // raw pointer — const does NOT propagate
};

void section_problem() {
    std::cout << "\n=== Problem: const does not propagate through pointer ===\n";
    const WithRawPtr obj(42);
    std::cout << "Before: " << obj.get_value() << "\n"; // 42
    obj.set_value(99);  // silently mutates through const method!
    std::cout << "After:  " << obj.get_value() << "\n"; // 99 — surprising!
}

// ─── SECTION 2: Same issue with unique_ptr ───────────────────────────────────

class WithUniquePtr {
public:
    explicit WithUniquePtr(int v) : data_(std::make_unique<InnerData>(v)) {}

    void set_value(int v) const {
        data_->value = v;  // STILL compiles — unique_ptr also doesn't propagate const
    }

    int get_value() const { return data_->value; }

private:
    std::unique_ptr<InnerData> data_;
};

// ─── SECTION 3: Manual propagate_const — roll your own if TS not available ───

template<typename T>
class prop_const {
public:
    explicit prop_const(T* ptr) : ptr_(ptr) {}

    // Non-const access → returns non-const pointer
    T* operator->()       { return ptr_; }
    T& operator*()        { return *ptr_; }

    // Const access → returns const pointer
    const T* operator->() const { return ptr_; }
    const T& operator*()  const { return *ptr_; }

private:
    T* ptr_;
};

class WithPropConst {
public:
    explicit WithPropConst(int v) : data_(new InnerData{v}) {}
    ~WithPropConst() { delete data_.operator->(); }  // cleanup (simplified)

    void set_value(int v) const {
        // data_->value = v;  // DOES NOT COMPILE — const propagated! ✓
        (void)v;
        std::cout << "(set_value blocked by const propagation)\n";
    }

    void set_value_mutable(int v) {
        data_->value = v;  // OK in non-const method
    }

    int get_value() const { return data_->value; }

private:
    prop_const<InnerData> data_;
};

void section_solution() {
    std::cout << "\n=== Solution: propagate_const wrapper ===\n";
    WithPropConst obj(42);
    std::cout << "Value: " << obj.get_value() << "\n"; // 42
    obj.set_value_mutable(100);
    std::cout << "After mutable set: " << obj.get_value() << "\n"; // 100

    const WithPropConst cobj(42);
    cobj.set_value(99);  // blocked by prop_const
    std::cout << "Const obj value: " << cobj.get_value() << "\n"; // 42
}

// ─── SECTION 4: Pimpl + propagate_const in context ───────────────────────────

// The Pimpl (Pointer to Implementation) pattern hides implementation details
// to reduce compile times and binary coupling.
// propagate_const is the correct way to keep const-safety in Pimpl.

struct OrderEngineImpl {
    int order_count = 0;
    double fill_rate = 0.0;
};

class OrderEngine {
public:
    OrderEngine() : impl_(new OrderEngineImpl{}) {}
    ~OrderEngine() { delete impl_.operator->(); }

    void place_order() {           // non-const — modifies impl
        impl_->order_count++;
    }

    int order_count() const {      // const — must not modify impl
        // impl_->order_count++;   // BLOCKED by prop_const — good!
        return impl_->order_count;
    }

private:
    prop_const<OrderEngineImpl> impl_;
};

void section_pimpl() {
    std::cout << "\n=== Pimpl + propagate_const ===\n";
    OrderEngine engine;
    engine.place_order();
    engine.place_order();
    std::cout << "Orders placed: " << engine.order_count() << "\n"; // 2
}

int main() {
    section_problem();
    section_solution();
    section_pimpl();
}
