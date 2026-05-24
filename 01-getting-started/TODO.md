# Repo Chapter 1 — Getting Started (Educative course prelude)

> **Heads up:** The PDF book's *Chapter 1* ("A Brief Introduction to C++") is in
> [`../02-brief-introduction-to-cpp/`](../02-brief-introduction-to-cpp/), not here.
> This folder mirrors the Educative course's "Getting Started" pre-chapter — it's
> tooling setup, not book content.

## Level 1 — Toolchain ready before any chapter compiles

- [ ] **L1.1** Compiler with C++20+ (`g++ --version` shows GCC 12+, or `clang++ --version` shows Clang 14+).
- [ ] **L1.2** CMake ≥ 3.20 (`cmake --version`).
- [ ] **L1.3** Install Google Test (`brew install googletest` on macOS, or build from source: https://github.com/google/googletest).
- [ ] **L1.4** Install Google Benchmark (`brew install google-benchmark` on macOS, or https://github.com/google/benchmark).
- [ ] **L1.5** Verify with a smoke test:
      ```bash
      cd ../02-brief-introduction-to-cpp/code
      cmake -B build -DCMAKE_BUILD_TYPE=Release
      cmake --build build -j
      ./build/Chapter02-A_Brief_Introduction_to_C++
      ```
      Expect: all gtest tests pass.

## Level 2 — Editor & profiling

- [ ] **L2.1** clangd or compile_commands.json wired into your editor.
- [ ] **L2.2** `perf` (Linux) or Instruments (macOS) installed for later chapters.
- [ ] **L2.3** Quick test: compile `02-brief-introduction-to-cpp/code/abstractions.cpp` with `-O2 -S` and inspect the assembly.

## Done when
- All toolchain checks pass.
- The Chapter 2 gtest binary builds and runs locally.
