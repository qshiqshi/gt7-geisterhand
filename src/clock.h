// One clock for all threads: seconds since the program started, never running backwards.
#pragma once
#include <chrono>

inline double monotonicSeconds() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}
