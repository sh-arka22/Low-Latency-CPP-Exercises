/**
 * Chapter 3 — Measuring Performance
 * Topic 1: Asymptotic Complexity & Big O Notation
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra 01_big_o.cpp -o big_o
 *
 * Key ideas:
 *   - Big O expresses the *growth rate* of runtime vs input size n
 *   - We keep only the highest-order term and drop constant factors
 *   - Rule: choose algorithm first, micro-optimize second
 */

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <numeric>   // std::iota
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <cmath>

// ─── helpers ────────────────────────────────────────────────────────────────

using Clock = std::chrono::steady_clock;

// Returns elapsed nanoseconds for one call to f(args...)
template <typename F, typename... Args>
long long time_ns(F&& f, Args&&... args) {
    auto t0 = Clock::now();
    f(std::forward<Args>(args)...);
    auto t1 = Clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
}

// ─── SECTION 1: O(n) — linear search ────────────────────────────────────────
// Growth rate: doubling n doubles runtime in the worst case.

bool linear_search_int(const std::vector<int>& vals, int key) {
    for (const auto& v : vals) {   // loops at most n times → O(n)
        if (v == key) return true;
    }
    return false;
}

struct Point { int x{}, y{}; };

bool linear_search_point(const std::vector<Point>& a, const Point& key) {
    for (size_t i = 0; i < a.size(); ++i) {  // still O(n): constant work per element
        if (a[i].x == key.x && a[i].y == key.y) return true;
    }
    return false;
}

void section_linear_search() {
    std::cout << "\n=== SECTION 1: O(n) Linear Search ===\n";
    for (int n : {100, 1'000, 10'000, 100'000}) {
        std::vector<int> v(n, 42);
        v.back() = 99;           // key only at the end → worst case
        auto ns = time_ns([&]{ linear_search_int(v, 99); });
        std::cout << "n=" << n << "  " << ns << " ns\n";
    }
    // OBSERVATION: runtime should roughly double each time n doubles.
}

// ─── SECTION 2: O(log n) — binary search ────────────────────────────────────
// Growth rate: doubling n adds only 1 more iteration.

bool binary_search_int(const std::vector<int>& a, int key) {
    if (a.empty()) return false;
    size_t low = 0, high = a.size() - 1;
    while (low <= high) {
        const size_t mid = low + ((high - low) / 2);  // avoids overflow
        if      (a[mid] < key) low  = mid + 1;
        else if (a[mid] > key) high = mid - 1;
        else                   return true;
    }
    return false;
}

void section_binary_search() {
    std::cout << "\n=== SECTION 2: O(log n) Binary Search ===\n";
    for (int n : {100, 1'000, 10'000, 100'000, 1'000'000}) {
        std::vector<int> v(n);
        std::iota(v.begin(), v.end(), 0);  // sorted: 0, 1, 2, ..., n-1
        auto ns = time_ns([&]{ binary_search_int(v, n - 1); }); // worst case: key at end
        std::cout << "n=" << n << "  " << ns << " ns\n";
    }
    // OBSERVATION: runtime grows very slowly — roughly +constant per 2× of n.
}

// ─── SECTION 3: O(n²) — insertion sort ──────────────────────────────────────
// Derivation: outer loop × inner loop = 1+2+…+(n-1) = n(n-1)/2 ≈ n²/2 → O(n²)

void insertion_sort(std::vector<int>& a) {
    for (size_t i = 1; i < a.size(); ++i) {
        size_t j = i;
        while (j > 0 && a[j-1] > a[j]) {
            std::swap(a[j], a[j-1]);
            --j;
        }
    }
}

void section_insertion_sort() {
    std::cout << "\n=== SECTION 3: O(n²) Insertion Sort ===\n";
    for (int n : {100, 500, 1'000, 5'000}) {
        // worst case: reverse-sorted input
        std::vector<int> v(n);
        std::iota(v.rbegin(), v.rend(), 0);
        auto ns = time_ns([&]{ insertion_sort(v); });
        std::cout << "n=" << n << "  " << ns << " ns\n";
    }
    // OBSERVATION: quadrupling n → roughly 16× runtime (n² relationship).
}

// ─── SECTION 4: Growth rate comparison ──────────────────────────────────────
// Shows O(1) vs O(log n) vs O(n) vs O(n²) side by side

void section_growth_rates() {
    std::cout << "\n=== SECTION 4: Growth Rate Comparison ===\n";
    std::cout << "n\t\tO(1)\tO(log n)\tO(n)\t\tO(n²)\n";
    for (long long n : {10LL, 100LL, 1'000LL, 10'000LL}) {
        long long log_n = 0;
        long long tmp = n;
        while (tmp > 1) { tmp /= 2; ++log_n; }   // approximate log2(n)
        std::cout << n << "\t\t"
                  << 1        << "\t"
                  << log_n    << "\t\t"
                  << n        << "\t\t"
                  << n * n    << "\n";
    }
}

// ─── SECTION 5: std::sort comparison (O(n log n)) ──────────────────────────
void section_std_sort() {
    std::cout << "\n=== SECTION 5: O(n log n) std::sort ===\n";
    for (int n : {10'000, 100'000, 1'000'000}) {
        std::vector<int> v(n);
        std::iota(v.rbegin(), v.rend(), 0);   // worst case: reverse-sorted
        auto ns = time_ns([&]{ std::sort(v.begin(), v.end()); });
        std::cout << "n=" << n << "  " << ns << " ns\n";
    }
    // OBSERVATION: going from n=10k to n=100k (10×) → time grows ~10×(log10/log1) ≈ 11-13×
}

// ─── TODO stubs (your exercises) ────────────────────────────────────────────

/* 
 * Math Questions Answers:
 * Q1: Given f(n) = 5n³ + 10n² + 1000n + 42, write the Big O.
 * A: Highest order term is n³, constants are dropped, so O(n³).
 * 
 * Q2: std::sort's complexity guarantee?
 * A: O(n log n). Modern std::sort uses Introsort (QuickSort + HeapSort fallback),
 *    guaranteeing worst-case O(n log n) comparisons.
 */

// Level 2: implement this — two nested loops, derive Big O manually first
// Big O derivation: outer loop runs n times. inner loop runs n times. n * n = n². Thus O(n²).
void nested_loop_complexity() {
    std::cout << "\n=== Level 2: Nested Loop Complexity (O(n²)) ===\n";
    for (int n : {1000, 2000, 4000, 8000}) {
        auto ns = time_ns([&]{
            volatile long sum = 0;
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                    sum += (i * j);
                }
            }
        });
        std::cout << "n=" << n << "  " << ns / 1000000 << " ms\n";
    }
}

void log_n_demo() {
    std::cout << "\n=== Level 2: log(n) Halving Demo ===\n";
    for (int n : {1000, 1000000, 1000000000}) {
        int count = 0;
        int current = n;
        while (current > 1) {
            current /= 2;
            ++count;
        }
        std::cout << "n=" << n << " -> halves to reach 1: " << count 
                  << " (log2(n) = " << std::log2(n) << ")\n";
    }
}

// Level 3: implement order_book_lookup with map vs unordered_map comparison
void order_book_lookup_benchmark() {
    std::cout << "\n=== Level 3: order_book_lookup benchmark ===\n";
    for (int n : {100, 10'000, 1'000'000}) {
        std::map<int, int> ordered_book;
        std::unordered_map<int, int> hash_book;
        
        for(int i = 0; i < n; ++i) {
            ordered_book[i] = i * 10;
            hash_book[i] = i * 10;
        }

        int target = n - 1; // lookup last inserted

        auto ns_map = time_ns([&]{
            volatile int val = ordered_book.find(target)->second;
            (void)val;
        });

        auto ns_hash = time_ns([&]{
            volatile int val = hash_book.find(target)->second;
            (void)val;
        });

        std::cout << "n=" << n << " | map O(log n): " << ns_map 
                  << " ns | unordered_map O(1): " << ns_hash << " ns\n";
    }
}

void n_log_n_verification() {
    std::cout << "\n=== Level 3: O(n log n) Verification ===\n";
    for (int n : {10'000, 100'000, 1'000'000}) {
        std::vector<int> v(n);
        std::iota(v.rbegin(), v.rend(), 0);
        auto ns = time_ns([&]{ std::sort(v.begin(), v.end()); });
        double ratio = (double)ns / (n * std::log2(n));
        std::cout << "n=" << n << "  " << ns / 1'000'000.0 << " ms | ns / (n log n) ratio: " << ratio << "\n";
    }
}

// ─── main ────────────────────────────────────────────────────────────────────
int main() {
    section_linear_search();
    section_binary_search();
    section_insertion_sort();
    section_growth_rates();
    section_std_sort();

    nested_loop_complexity();
    log_n_demo();
    order_book_lookup_benchmark();
    n_log_n_verification();

    std::cout << "\n✓ All sections complete.\n";
    return 0;
}
