// A made-up drive for the demo and the self-test: steering as on a twisty lap, in degrees,
// positive = right. Mostly calm sweeps, small corrections, and a quick flick of opposite lock
// every few seconds.
#pragma once
#include <cmath>

inline double demoSteeringDeg(double t) {
    const double sweep = 95.0 * std::sin(0.50 * t) * std::sin(0.17 * t + 1.0) + 40.0 * std::sin(1.3 * t + 0.4);
    const double corrections = 6.0 * std::sin(4.1 * t) + 3.0 * std::sin(9.7 * t + 2.0);
    const double phase = std::fmod(t, 7.3) - 3.0;
    const double flick = 30.0 * std::exp(-phase * phase / (2.0 * 0.12 * 0.12)) * (std::fmod(t, 14.6) < 7.3 ? 1.0 : -1.0);
    return sweep + corrections + flick;
}
