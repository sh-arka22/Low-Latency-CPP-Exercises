// Chapter 7 — Memory Management
// Topic 7: Small Buffer Optimization (SSO, SBO, SmallVec, SmallFunction)
// Compile: g++ -std=c++20 -O2 -o 07 07_small_buffer_optimization.cpp
// Run:     ./07

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <new>
#include <string>
#include <type_traits>

static inline uint64_t rdtsc() {
#if defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return static_cast<uint64_t>(hi) << 32 | lo;
#else
    return 0;
#endif
}

template <typename T>
void sink(T const& v) { asm volatile("" : : "r,m"(v) : "memory"); }

// ─── 1. std::string SSO anatomy ──────────────────────────────────────────────

// Check if a string is using SSO (data pointer inside the string object):
bool is_sso(const std::string& s) {
    const char* data = s.data();
    const char* obj  = reinterpret_cast<const char*>(&s);
    return (data >= obj) && (data < obj + sizeof(s));
}

void demo_sso() {
    std::cout << "--- std::string SSO ---\n";
    std::cout << "sizeof(std::string) = " << sizeof(std::string) << "\n";

    // Find the SSO threshold by incrementally growing the string
    std::string s;
    size_t threshold = 0;
    for (size_t len = 0; len <= 32; ++len) {
        s = std::string(len, 'x');
        if (!is_sso(s)) {
            threshold = len;
            break;
        }
    }
    std::cout << "SSO threshold (first heap alloc): " << threshold << " chars\n";

    std::string small{"AAPL"};    // 4 chars — SSO
    std::string large{"VERYLONGSTRINGTHATWILLNOTFITINSSO"};  // 33+ chars — heap

    std::cout << "\"AAPL\" (4 chars):   SSO = " << is_sso(small) << "\n";
    std::cout << "\"VERY...\" (33 chars): SSO = " << is_sso(large) << "\n\n";
}

// ─── 2. std::function SBO ────────────────────────────────────────────────────

// Track allocations to see when std::function uses heap
static int g_alloc = 0;
void* operator new(size_t n)  { ++g_alloc; return std::malloc(n); }
void  operator delete(void* p) noexcept { std::free(p); }
void  operator delete(void* p, size_t) noexcept { std::free(p); }

void demo_std_function_sbo() {
    std::cout << "--- std::function SBO ---\n";

    // Small lambda — stays in SBO buffer (no heap):
    g_alloc = 0;
    {
        std::function<int()> f = []{ return 42; };
        sink(f());
    }
    std::cout << "Empty lambda in std::function: " << g_alloc << " heap alloc(s)\n";

    // Large capture — overflows SBO, goes to heap:
    g_alloc = 0;
    {
        // 128 bytes of capture data — definitely overflows SBO
        std::array<char, 128> big{};
        std::function<int()> f = [big]{ return big[0]; };
        sink(f());
    }
    std::cout << "Large capture (128B) in std::function: " << g_alloc << " heap alloc(s)\n\n";
}

// ─── 3. SmallFunction<Sig, BufSize> — zero-heap guaranteed ───────────────────

// Compile-time guarantee: if the callable overflows the buffer → compile error,
// not a silent heap allocation.

template <typename Sig, std::size_t BufSize = 48>
class SmallFunction;

template <typename R, typename... Args, std::size_t BufSize>
class SmallFunction<R(Args...), BufSize> {
    alignas(std::max_align_t) char buf_[BufSize]{};
    R (*invoke_)(void*, Args...) {nullptr};
    void (*destroy_)(void*) {nullptr};

public:
    SmallFunction() = default;

    template <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, SmallFunction>>>
    SmallFunction(F&& f) {
        // COMPILE-TIME GUARANTEE: if this fails, the lambda is too big.
        static_assert(sizeof(F) <= BufSize,
            "SmallFunction: callable too large for inline buffer. "
            "Increase BufSize or reduce captures.");
        static_assert(alignof(F) <= alignof(std::max_align_t));

        new (buf_) F(std::forward<F>(f));

        invoke_  = [](void* b, Args... a) -> R {
            return (*reinterpret_cast<F*>(b))(std::forward<Args>(a)...);
        };
        destroy_ = [](void* b) {
            reinterpret_cast<F*>(b)->~F();
        };
    }

    ~SmallFunction() {
        if (destroy_) destroy_(buf_);
    }

    // Move-only: copying would require invoker to deep-copy the callable
    SmallFunction(const SmallFunction&) = delete;
    SmallFunction& operator=(const SmallFunction&) = delete;

    SmallFunction(SmallFunction&& o) noexcept {
        std::memcpy(buf_, o.buf_, BufSize);
        invoke_  = o.invoke_;
        destroy_ = o.destroy_;
        o.invoke_ = o.destroy_ = nullptr;
    }

    R operator()(Args... args) {
        assert(invoke_ && "SmallFunction called on empty object");
        return invoke_(buf_, std::forward<Args>(args)...);
    }

    explicit operator bool() const noexcept { return invoke_ != nullptr; }
};

void demo_small_function() {
    std::cout << "--- SmallFunction<int(), 48> ---\n";

    g_alloc = 0;
    {
        SmallFunction<int()> f = []{ return 42; };
        std::cout << "  Result: " << f() << "\n";
    }
    std::cout << "  Heap allocs: " << g_alloc << " (should be 0)\n";

    // This would be a compile error (lambda too large for 48 bytes):
    // std::array<char, 100> big{};
    // SmallFunction<int()> f = [big]{ return big[0]; };  // static_assert fires!

    std::cout << "\n";
}

// ─── 4. SmallVec<T, N> ───────────────────────────────────────────────────────

template <typename T, std::size_t InlineCapacity>
class SmallVec {
    alignas(T) char inline_buf_[sizeof(T) * InlineCapacity]{};
    T*      data_{reinterpret_cast<T*>(inline_buf_)};
    std::size_t size_{0};
    std::size_t cap_{InlineCapacity};
    bool on_heap_{false};

    void grow() {
        std::size_t new_cap = cap_ * 2;
        T* new_data = static_cast<T*>(::operator new(sizeof(T) * new_cap));
        for (std::size_t i = 0; i < size_; ++i) {
            new (new_data + i) T(std::move(data_[i]));
            if constexpr (!std::is_trivially_destructible_v<T>)
                data_[i].~T();
        }
        if (on_heap_) ::operator delete(data_);
        data_ = new_data;
        cap_  = new_cap;
        on_heap_ = true;
    }

public:
    SmallVec() = default;
    ~SmallVec() {
        for (std::size_t i = 0; i < size_; ++i)
            if constexpr (!std::is_trivially_destructible_v<T>)
                data_[i].~T();
        if (on_heap_) ::operator delete(data_);
    }

    void push_back(T val) {
        if (size_ == cap_) grow();
        new (data_ + size_++) T(std::move(val));
    }

    T& operator[](std::size_t i) { return data_[i]; }
    std::size_t size() const { return size_; }
    bool on_heap() const { return on_heap_; }
};

void demo_small_vec() {
    std::cout << "--- SmallVec<int, 8> ---\n";
    SmallVec<int, 8> v;

    for (int i = 0; i < 8; ++i) v.push_back(i);
    std::cout << "  After 8 pushes: on_heap=" << v.on_heap() << "\n";

    g_alloc = 0;
    v.push_back(8);   // triggers heap allocation (9th element)
    std::cout << "  After 9th push: on_heap=" << v.on_heap()
              << ", heap allocs=" << g_alloc << "\n";

    std::cout << "  Values: ";
    for (std::size_t i = 0; i < v.size(); ++i) std::cout << v[i] << " ";
    std::cout << "\n\n";
}

// ─── 5. SmallString for trading symbols ──────────────────────────────────────

struct SmallString {
    static constexpr std::size_t MAX = 15;
    char  buf[MAX + 1]{};
    uint8_t len{0};

    SmallString() = default;
    SmallString(const char* s) {
        len = static_cast<uint8_t>(std::min(std::strlen(s), MAX));
        std::memcpy(buf, s, len);
        buf[len] = '\0';
    }

    bool operator==(const SmallString& o) const noexcept {
        return len == o.len && std::memcmp(buf, o.buf, len) == 0;
    }
    bool operator<(const SmallString& o) const noexcept {
        return std::strncmp(buf, o.buf, MAX) < 0;
    }
    const char* c_str() const noexcept { return buf; }
};

static_assert(sizeof(SmallString) == 17, "SmallString fits in <32 bytes");
static_assert(std::is_trivially_copyable_v<SmallString>, "can memcpy SmallString");

void demo_small_string() {
    std::cout << "--- SmallString for symbols ---\n";
    std::cout << "sizeof(SmallString) = " << sizeof(SmallString) << "\n";

    SmallString aapl{"AAPL"};
    SmallString msft{"MSFT"};

    std::cout << "  aapl: " << aapl.c_str() << "\n";
    std::cout << "  msft: " << msft.c_str() << "\n";
    std::cout << "  aapl < msft: " << (aapl < msft) << "\n";
    std::cout << "  aapl == aapl: " << (aapl == aapl) << "\n";
    std::cout << "  trivially_copyable: " << std::is_trivially_copyable_v<SmallString> << "\n\n";
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Small Buffer Optimization ===\n\n";
    demo_sso();
    demo_std_function_sbo();
    demo_small_function();
    demo_small_vec();
    demo_small_string();

    std::cout << "=== Rules ===\n"
              << "  1. std::string SSO avoids heap for short strings (≤15 or ≤22 chars).\n"
              << "  2. std::function can heap-allocate for large captures — use SmallFunction.\n"
              << "  3. SmallFunction: static_assert on overflow → compile error, not silent alloc.\n"
              << "  4. SmallVec<T,N>: N inline elements → zero heap until overflow.\n"
              << "  5. SmallString: for fixed-max symbols, trivially_copyable, zero heap.\n";
    return 0;
}
