// Milestone 1: prove the test harness compiles and runs in CI.
// Real engine tests (offline render through the NAM stage chain) arrive in
// milestone 2 alongside src/engine/.

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{
// Placeholder for the smoothing math the engine relies on: a linear ramp of
// N steps from a to b must land on b exactly, not drift.
float linearRampEnd(float start, float target, int steps)
{
    float value = start;
    const float increment = (target - start) / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i)
        value += increment;
    return value;
}
} // namespace

TEST_CASE("test harness runs")
{
    REQUIRE(true);
}

TEST_CASE("linear ramp lands near its target")
{
    const float end = linearRampEnd(0.0f, 1.0f, 960); // 20 ms at 48 kHz
    REQUIRE(std::abs(end - 1.0f) < 1.0e-4f);
}
