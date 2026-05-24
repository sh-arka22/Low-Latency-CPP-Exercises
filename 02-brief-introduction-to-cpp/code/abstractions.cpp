// Chapter 1 / Repo Ch.2  ·  Section 2.1 — Zero-cost abstractions
//
// Demonstrates the same task ("how many Hamlets in this list?") written in
// idiomatic C and in idiomatic C++. Both compile down to roughly the same
// machine code; the C++ version simply lifts the bookkeeping into the type
// system + std::count.
//
// Build (standalone):
//   g++ -std=c++20 -O2 -o abstractions abstractions.cpp -lgtest -lgtest_main -lpthread
//
// Or via the CMakeLists.txt in this folder (mirrors PacktPublishing/
// Cpp-High-Performance-Second-Edition/Chapter01/CMakeLists.txt).

#include <gtest/gtest.h>
#include <cstring>     // strcmp
#include <algorithm>   // std::count
#include <forward_list>
#include <string>

namespace {

// ---- C version ----------------------------------------------------------
struct string_elem_t {
    const char*    str_;
    string_elem_t* next_;
};

int num_hamlet(string_elem_t* books) {
    const char* hamlet = "Hamlet";
    int n = 0;
    for (string_elem_t* b = books; b != nullptr; b = b->next_)
        if (std::strcmp(b->str_, hamlet) == 0)
            ++n;
    return n;
}

// ---- C++ version --------------------------------------------------------
auto num_hamlet(const std::forward_list<std::string>& books) {
    return std::count(books.begin(), books.end(), "Hamlet");
}

} // namespace

TEST(Abstractions, NumHamlet_CVersion) {
    string_elem_t a{"Hamlet",           nullptr};
    string_elem_t b{"Romeo and Juliet", nullptr};
    string_elem_t c{"Hamlet",           nullptr};
    a.next_ = &b;
    b.next_ = &c;
    EXPECT_EQ(num_hamlet(&a), 2);
}

TEST(Abstractions, NumHamlet_CppVersion) {
    auto books = std::forward_list<std::string>{
        "Hamlet", "Romeo and Juliet", "Hamlet"
    };
    EXPECT_EQ(num_hamlet(books), 2);
}

// TODO (L2.1.b): compile with -O2 -S and diff the two num_hamlet bodies.
//                count the extra instructions in the C++ version (target: 0–2).
