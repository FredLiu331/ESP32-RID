#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "rid/model.hpp"

namespace rid {

enum class TrajectoryType : uint8_t {
    Hover,
    Line,
    Circle,
    Rectangle,
    WaypointLoop,
    TakeoffCruiseLand,
};

struct EnuPoint {
    float east_m;
    float north_m;
    float up_m;
};

struct TrajectoryConfig {
    TrajectoryType type{TrajectoryType::Hover};
    float altitude_m{0.0F};
    float speed_mps{0.0F};
    float heading_deg{0.0F};
    float primary_size_m{0.0F};
    float secondary_size_m{0.0F};
    float vertical_speed_mps{0.0F};
    uint32_t cruise_time_ms{0};
    std::vector<EnuPoint> waypoints;
};

TrajectoryConfig hover_trajectory(float altitude_m);
TrajectoryConfig line_trajectory(float length_m, float speed_mps, float heading_deg,
                                 float altitude_m);
TrajectoryConfig circle_trajectory(float radius_m, float speed_mps, float altitude_m);
TrajectoryConfig rectangle_trajectory(float width_m, float height_m, float speed_mps,
                                      float altitude_m);
TrajectoryConfig waypoint_trajectory(std::vector<EnuPoint> waypoints, float speed_mps);
TrajectoryConfig takeoff_cruise_land_trajectory(float altitude_m, float vertical_speed_mps,
                                                uint32_t cruise_time_ms);
bool valid(const TrajectoryConfig &config);

class TrajectoryEngine {
public:
    explicit TrajectoryEngine(GeoPoint site_center);

    FlightState sample(const TrajectoryConfig &config, uint16_t instance_index,
                       uint32_t elapsed_ms) const;

private:
    uint64_t extend_elapsed(uint16_t instance_index, uint32_t elapsed_ms) const;

    GeoPoint site_center_;
    mutable std::array<uint32_t, 50> last_elapsed_{};
    mutable std::array<uint64_t, 50> wrap_offset_{};
    mutable std::array<bool, 50> elapsed_initialized_{};
};

}  // namespace rid
