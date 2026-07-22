#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "rid/trajectory.hpp"
#include "unity.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr rid::GeoPoint kSite{31.2304, 121.4737};

}  // namespace

TEST_CASE("hover remains fixed", "[trajectory]") {
    rid::TrajectoryEngine engine{kSite};
    const auto config = rid::hover_trajectory(80.0F);
    const auto start = engine.sample(config, 0, 0);
    const auto later = engine.sample(config, 0, 60000);

    TEST_ASSERT_DOUBLE_WITHIN(1e-10, start.latitude_deg, later.latitude_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-10, start.longitude_deg, later.longitude_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-5F, 80.0F, later.height_agl_m);
    TEST_ASSERT_TRUE(later.airborne);
    TEST_ASSERT_EQUAL_UINT32(0, start.relative_time_ms);
}

TEST_CASE("line reverses and rectangle visits corners", "[trajectory]") {
    rid::TrajectoryEngine line_engine{kSite};
    const auto line = rid::line_trajectory(100.0F, 10.0F, 90.0F, 60.0F);
    const auto line_start = line_engine.sample(line, 0, 0);
    const auto line_turn = line_engine.sample(line, 0, 10000);
    const auto line_end = line_engine.sample(line, 0, 20000);
    TEST_ASSERT_TRUE(line_turn.longitude_deg > line_start.longitude_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-8, line_start.latitude_deg, line_end.latitude_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-8, line_start.longitude_deg, line_end.longitude_deg);

    rid::TrajectoryEngine rectangle_engine{kSite};
    const auto rectangle = rid::rectangle_trajectory(40.0F, 20.0F, 10.0F, 70.0F);
    const auto southwest = rectangle_engine.sample(rectangle, 0, 0);
    const auto southeast = rectangle_engine.sample(rectangle, 0, 4000);
    const auto northeast = rectangle_engine.sample(rectangle, 0, 6000);
    const auto looped = rectangle_engine.sample(rectangle, 0, 12000);
    TEST_ASSERT_TRUE(southeast.longitude_deg > southwest.longitude_deg);
    TEST_ASSERT_TRUE(northeast.latitude_deg > southeast.latitude_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-8, southwest.latitude_deg, looped.latitude_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-8, southwest.longitude_deg, looped.longitude_deg);
}

TEST_CASE("circle uses WGS84 and deterministic instance phase", "[trajectory]") {
    rid::TrajectoryEngine engine{kSite};
    const auto circle = rid::circle_trajectory(50.0F, 5.0F, 80.0F);
    const auto start = engine.sample(circle, 0, 0);
    const uint32_t period_ms =
        static_cast<uint32_t>((2.0 * kPi * 50.0 / 5.0) * 1000.0);
    const auto end = engine.sample(circle, 0, period_ms);
    const auto phased = engine.sample(circle, 1, 0);

    constexpr double kA = 6378137.0;
    constexpr double kE2 = 6.69437999014e-3;
    const double latitude_rad = kSite.latitude_deg * kPi / 180.0;
    const double prime_vertical = kA / std::sqrt(1.0 - kE2 * std::sin(latitude_rad) *
                                                          std::sin(latitude_rad));
    const double expected_longitude =
        kSite.longitude_deg + 50.0 / (prime_vertical * std::cos(latitude_rad)) * 180.0 / kPi;
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, expected_longitude, start.longitude_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, start.latitude_deg, end.latitude_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, start.longitude_deg, end.longitude_deg);
    TEST_ASSERT_TRUE(std::fabs(phased.latitude_deg - start.latitude_deg) > 1e-8 ||
                     std::fabs(phased.longitude_deg - start.longitude_deg) > 1e-8);
}

TEST_CASE("waypoints interpolate and loop", "[trajectory]") {
    rid::TrajectoryEngine engine{kSite};
    const auto waypoints = rid::waypoint_trajectory(
        std::vector<rid::EnuPoint>{{0.0F, 0.0F, 20.0F},
                                   {10.0F, 0.0F, 30.0F},
                                   {10.0F, 10.0F, 40.0F}},
        10.0F);
    const auto first = engine.sample(waypoints, 0, 0);
    const auto second = engine.sample(waypoints, 0, 1000);
    const auto third = engine.sample(waypoints, 0, 2000);
    const uint32_t loop_ms = static_cast<uint32_t>((2.0 + std::sqrt(2.0)) * 1000.0);
    const auto looped = engine.sample(waypoints, 0, loop_ms);

    TEST_ASSERT_FLOAT_WITHIN(0.01F, 20.0F, first.height_agl_m);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 30.0F, second.height_agl_m);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 40.0F, third.height_agl_m);
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, first.latitude_deg, looped.latitude_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, first.longitude_deg, looped.longitude_deg);
}

TEST_CASE("takeoff cruise landing changes flight state", "[trajectory]") {
    rid::TrajectoryEngine engine{kSite};
    const auto mission = rid::takeoff_cruise_land_trajectory(20.0F, 5.0F, 2000);

    const auto ground = engine.sample(mission, 0, 0);
    const auto climbing = engine.sample(mission, 0, 2000);
    const auto cruise = engine.sample(mission, 0, 5000);
    const auto descending = engine.sample(mission, 0, 8000);
    const auto landed = engine.sample(mission, 0, 10000);
    TEST_ASSERT_FALSE(ground.airborne);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 10.0F, climbing.height_agl_m);
    TEST_ASSERT_TRUE(climbing.airborne);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 20.0F, cruise.height_agl_m);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 10.0F, descending.height_agl_m);
    TEST_ASSERT_FALSE(landed.airborne);
}

TEST_CASE("32 bit elapsed time wrap remains continuous", "[trajectory]") {
    rid::TrajectoryEngine engine{kSite};
    const auto line = rid::line_trajectory(100.0F, 5.0F, 0.0F, 50.0F);
    const auto before = engine.sample(line, 0, std::numeric_limits<uint32_t>::max() - 10U);
    const auto after = engine.sample(line, 0, 9U);

    TEST_ASSERT_DOUBLE_WITHIN(2e-6, before.latitude_deg, after.latitude_deg);
    TEST_ASSERT_DOUBLE_WITHIN(2e-6, before.longitude_deg, after.longitude_deg);
    TEST_ASSERT_EQUAL_UINT32(9, after.relative_time_ms);
}
