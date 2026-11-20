#include "../src/ring_geometry.h"

#include <cassert>
#include <cmath>

int main() {
    using namespace ring_geometry;
    assert(ClampPercent(-1) == 0);
    assert(ClampPercent(0) == 0);
    assert(ClampPercent(50) == 50);
    assert(ClampPercent(100) == 100);
    assert(ClampPercent(140) == 100);

    assert(std::abs(SweepRadians(0)) < 1e-12);
    assert(std::abs(SweepRadians(25) - kPi / 2.0) < 1e-12);
    assert(std::abs(SweepRadians(50) - kPi) < 1e-12);
    assert(std::abs(SweepRadians(100) - 2.0 * kPi) < 1e-12);

    assert(std::abs(Interpolate(0, 100, 0) - 0) < 1e-12);
    assert(std::abs(Interpolate(0, 100, 0.5) - 50) < 1e-12);
    assert(std::abs(Interpolate(0, 100, 1) - 100) < 1e-12);
    assert(std::abs(Interpolate(20, 80, -1) - 20) < 1e-12);
    assert(std::abs(Interpolate(20, 80, 2) - 80) < 1e-12);
    return 0;
}
