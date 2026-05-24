// Chapter 1 / Repo Ch.2  ·  Section 2.2 — Memory layout (stack vs heap)
//
// Two ways C++ minimises allocations vs a Java-style design:
//   1) Put objects on the stack when their lifetime is the enclosing scope.
//   2) Put N objects in ONE heap allocation (contiguous vector<T>) rather
//      than N separate allocations of references-to-T.
//
// HFT angle: every pointer indirection is a potential cache miss. Iterating
// vector<Car> is linear prefetch; iterating vector<unique_ptr<Car>> is a
// pointer chase that the prefetcher cannot help with.

#include <gtest/gtest.h>
#include <vector>

struct Car {
    int doors_{};
};

// All on the stack — zero heap allocations.
auto some_func() {
    auto num_doors = 2;
    auto car1 = Car{num_doors};
    auto car2 = Car{num_doors};
    (void)car1; (void)car2;
}

// One heap allocation for the vector's buffer, holding 7 Cars contiguously.
// (Java's ArrayList<Car> equivalent = 1 + 7 = 8 allocations.)
auto car_list() {
    const auto n = 7;
    auto cars = std::vector<Car>{};
    cars.reserve(n);
    for (auto i = 0; i < n; ++i)
        cars.push_back(Car{});
    return cars;
}

TEST(HeapAllocations, StackCars) {
    some_func();
    SUCCEED();
}

TEST(HeapAllocations, ContiguousVector) {
    auto cars = car_list();
    EXPECT_EQ(cars.size(), 7u);
}

// TODO (L2.2.b): add a Google Benchmark microbenchmark comparing:
//   - iterate vector<Car> 1e6 elements
//   - iterate vector<unique_ptr<Car>> 1e6 elements
//   - iterate vector<Car_padded_to_4096_bytes>
// Record cycles per element. The numbers will be your "cache locality is real"
// permanent reference card.
