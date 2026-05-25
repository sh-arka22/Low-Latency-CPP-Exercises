/**
 * Chapter 2 — Essential C++ Techniques
 * Topic 2: Move Semantics — Value Categories & Rule of Five / Zero
 *
 * ── FIRST PRINCIPLES ──────────────────────────────────────────────────────────
 *
 * VALUE CATEGORIES (every expression is one of these):
 *
 *   lvalue  — has identity (name / address), persists beyond expression
 *              e.g.  int x;   x = 5;   // x is lvalue
 *
 *   rvalue  — no persistent identity; temporary / about to die
 *              e.g.  42, get_string(), std::move(x)
 *
 *   xvalue  — "eXpiring value" — result of std::move(); has identity BUT
 *              you've said "I'm done with it"; can be moved from
 *
 * COPY vs MOVE:
 *   Copy  → duplicate the resource         (O(n) allocation + memcpy)
 *   Move  → steal the pointer, null source (O(1), pointer juggling only)
 *
 * RULE OF FIVE: if you define ANY of these, define ALL five:
 *   1. Copy constructor       Buffer(const Buffer&)
 *   2. Copy assignment        Buffer& operator=(const Buffer&)
 *   3. Destructor             ~Buffer()
 *   4. Move constructor       Buffer(Buffer&&) noexcept
 *   5. Move assignment        Buffer& operator=(Buffer&&) noexcept
 *
 * RULE OF ZERO: prefer RAII members (unique_ptr, vector, string) so that
 *   the compiler-generated defaults do the right thing automatically.
 *
 * HFT NOTE:
 *   Move constructors MUST be `noexcept` for STL containers to use them
 *   during reallocation (std::vector::push_back fall-back copies if not noexcept).
 *   Missing noexcept → silent performance cliff.
 */

#include <algorithm>
#include <initializer_list>
#include <iostream>
#include <utility>  // std::move, std::exchange, std::swap
#include <vector>
#include <string>

// ─── SECTION 1: Rule of Five — raw resource owner ────────────────────────────

class Buffer {
public:
    // Default constructor
    Buffer(const std::initializer_list<float>& vals) : size_(vals.size()) {
        ptr_ = new float[size_];
        std::copy(vals.begin(), vals.end(), ptr_);
        std::cout << "[ctor] Buffer(" << size_ << ")\n";
    }

    // 1. Copy constructor — deep copy
    Buffer(const Buffer& rhs) : size_(rhs.size_) {
        ptr_ = new float[size_];
        std::copy(rhs.ptr_, rhs.ptr_ + size_, ptr_);
        std::cout << "[copy ctor]\n";
    }

    // 2. Copy assignment — deep copy with self-assignment guard
    Buffer& operator=(const Buffer& rhs) {
        if (this == &rhs) return *this;
        // allocate first (may throw) — only then release old memory
        auto tmp_sz  = rhs.size_;
        auto tmp_ptr = new float[tmp_sz];
        std::copy(rhs.ptr_, rhs.ptr_ + tmp_sz, tmp_ptr);
        delete[] ptr_;
        ptr_  = tmp_ptr;
        size_ = tmp_sz;
        std::cout << "[copy assign]\n";
        return *this;
    }

    // 3. Destructor
    ~Buffer() {
        delete[] ptr_;
        ptr_ = nullptr;
        std::cout << "[dtor]\n";
    }

    // 4. Move constructor — O(1): steal pointer, null source
    //    std::exchange(old, new) atomically returns old and sets to new
    Buffer(Buffer&& rhs) noexcept
        : size_(std::exchange(rhs.size_, 0))
        , ptr_ (std::exchange(rhs.ptr_,  nullptr))
    {
        std::cout << "[move ctor]\n";
    }

    // 5. Move assignment — O(1): steal and nullify
    Buffer& operator=(Buffer&& rhs) noexcept {
        if (this == &rhs) return *this;
        delete[] ptr_;
        ptr_  = std::exchange(rhs.ptr_,  nullptr);
        size_ = std::exchange(rhs.size_, 0);
        std::cout << "[move assign]\n";
        return *this;
    }

    auto begin() const { return ptr_; }
    auto end()   const { return ptr_ + size_; }
    auto size()  const { return size_; }

private:
    std::size_t size_ = 0;
    float*      ptr_  = nullptr;
};

void demo_rule_of_five() {
    std::cout << "\n=== Rule of Five ===\n";
    Buffer b0 = {0.0f, 0.5f, 1.0f};
    Buffer b1 = b0;              // copy ctor
    b0 = b1;                     // copy assign
    Buffer b2 = std::move(b0);   // move ctor — b0 is now empty
    b1 = std::move(b2);          // move assign
}

// ─── SECTION 2: Rule of Zero — let the compiler do it ────────────────────────

class SafeBuffer {
public:
    explicit SafeBuffer(std::initializer_list<float> vals)
        : data_(vals) {}

    // NO user-defined copy/move/dtor — compiler generates correct ones
    // std::vector handles deep copy, move (O(1) via pointer steal), and destruction

    auto begin() const { return data_.begin(); }
    auto end()   const { return data_.end(); }

private:
    std::vector<float> data_;  // RAII member → Rule of Zero applies
};

// ─── SECTION 3: value categories in practice ─────────────────────────────────

auto make_string() { return std::string("HFT"); }

class Widget {
public:
    // Overloading on value category:
    void set_name(const std::string& s) { name_ = s;            }  // copy
    void set_name(std::string&&      s) { name_ = std::move(s); }  // move

    // Better: accept by value, move in — one overload covers both
    void set_title(std::string s) { title_ = std::move(s); }

private:
    std::string name_;
    std::string title_;
};

void demo_value_categories() {
    std::cout << "\n=== Value categories ===\n";
    auto w = Widget{};

    std::string s = "Market Maker";
    w.set_name(s);                    // lvalue → copy
    w.set_name(std::move(s));         // xvalue → move (s is now valid-but-unspecified)
    w.set_name(make_string());        // rvalue → move (temporary)
    w.set_title("Order Manager");     // rvalue → constructed in-place, moved into member
}

// ─── SECTION 4: moving non-resource types — the trap ─────────────────────────

struct Menu {
    Menu(std::initializer_list<std::string> items) : items_(items) {}

    // FIX: swap index_ too, so moved-from object stays consistent
    Menu(Menu&& rhs) noexcept {
        std::swap(items_, rhs.items_);
        std::swap(index_, rhs.index_);
    }

    void select(int i) { index_ = i; }

    std::string selected() const {
        return (index_ != -1) ? items_[index_] : "(none)";
    }

private:
    std::vector<std::string> items_;
    int index_ = -1;
};

void demo_moving_non_resource() {
    std::cout << "\n=== Moving non-resource types ===\n";
    auto a = Menu{"New", "Open", "Close"};
    a.select(2);
    std::cout << "Before move: " << a.selected() << "\n";
    auto b = std::move(a);
    // Without swapping index_, a.index_ = 2 but a.items_ is empty → crash
    std::cout << "After move b: " << b.selected() << "\n";
    // a.selected() is now safe — index_ was swapped to -1
    std::cout << "After move a: " << a.selected() << "\n";
}

// ─── SECTION 5: ref-qualifiers on member functions ───────────────────────────

struct Foo {
    void func() &  { std::cout << "called on lvalue ref\n"; }
    void func() && { std::cout << "called on rvalue (expiring)\n"; }
};

void demo_ref_qualifiers() {
    std::cout << "\n=== Ref qualifiers ===\n";
    Foo f;
    f.func();               // lvalue → &
    std::move(f).func();    // xvalue → &&
    Foo{}.func();           // rvalue → &&
}

int main() {
    demo_rule_of_five();
    demo_value_categories();
    demo_moving_non_resource();
    demo_ref_qualifiers();
}
