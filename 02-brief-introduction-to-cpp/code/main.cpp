// Chapter 1 / Repo Ch.2 — gtest entry point.
// Identical to PacktPublishing/Cpp-High-Performance-Second-Edition/Chapter01/main.cpp
#include <gtest/gtest.h>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
