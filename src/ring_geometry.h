#pragma once
// Pure geometry helpers shared by the taskbar ring renderer and its tests.
#include <algorithm>
#include <cmath>

namespace ring_geometry {

constexpr double kPi = 3.14159265358979323846;

inline int ClampPercent(int percent) {
    return std::clamp(percent, 0, 100);
}

inline double SweepRadians(int percent) {
    return 2.0 * kPi * (ClampPercent(percent) / 100.0);
}

// Smoothstep interpolation. t is clamped so stale timer messages cannot overshoot.
inline double Interpolate(double from, double to, double t) {
    t = std::clamp(t, 0.0, 1.0);
    const double eased = t * t * (3.0 - 2.0 * t);
    return from + (to - from) * eased;
}

}  // namespace ring_geometry
