#include "rid/trajectory.hpp"

#include <cmath>
#include <utility>

namespace rid {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kWgs84A = 6378137.0;
constexpr double kWgs84E2 = 6.69437999014e-3;
constexpr double kMillisecondsPerSecond = 1000.0;

struct Motion {
    double east_m{0.0};
    double north_m{0.0};
    float altitude_m{0.0F};
    float horizontal_speed_mps{0.0F};
    float vertical_speed_mps{0.0F};
    float heading_deg{0.0F};
    bool airborne{false};
};

float normalize_heading(double heading_deg) {
    double normalized = std::fmod(heading_deg, 360.0);
    if (normalized < 0.0) normalized += 360.0;
    return static_cast<float>(normalized);
}

double positive_phase(double seconds, double period_seconds, uint16_t instance_index) {
    if (period_seconds <= 0.0) return 0.0;
    const double offset = period_seconds * (static_cast<double>(instance_index % 50U) / 50.0);
    return std::fmod(seconds + offset, period_seconds);
}

GeoPoint enu_to_wgs84(const GeoPoint &origin, double east_m, double north_m) {
    const double latitude_rad = origin.latitude_deg * kPi / 180.0;
    const double sin_latitude = std::sin(latitude_rad);
    const double denominator = 1.0 - kWgs84E2 * sin_latitude * sin_latitude;
    const double meridional_radius =
        kWgs84A * (1.0 - kWgs84E2) / std::pow(denominator, 1.5);
    const double prime_vertical_radius = kWgs84A / std::sqrt(denominator);

    return GeoPoint{
        origin.latitude_deg + north_m / meridional_radius * 180.0 / kPi,
        origin.longitude_deg + east_m / (prime_vertical_radius * std::cos(latitude_rad)) *
                                   180.0 / kPi,
    };
}

Motion sample_line(const TrajectoryConfig &config, double seconds, uint16_t instance_index) {
    const double length = config.primary_size_m;
    const double speed = config.speed_mps;
    const double period = speed > 0.0 ? 2.0 * length / speed : 0.0;
    const double phase = positive_phase(seconds, period, instance_index);
    const double travelled = phase * speed;
    const bool returning = travelled > length;
    const double position = returning ? 2.0 * length - travelled : travelled;
    const double heading_rad = static_cast<double>(config.heading_deg) * kPi / 180.0;
    return Motion{
        position * std::sin(heading_rad),
        position * std::cos(heading_rad),
        config.altitude_m,
        config.speed_mps,
        0.0F,
        normalize_heading(config.heading_deg + (returning ? 180.0 : 0.0)),
        config.altitude_m > 0.0F,
    };
}

Motion sample_circle(const TrajectoryConfig &config, double seconds, uint16_t instance_index) {
    const double radius = config.primary_size_m;
    const double speed = config.speed_mps;
    const double period = speed > 0.0 ? 2.0 * kPi * radius / speed : 0.0;
    const double phase = positive_phase(seconds, period, instance_index);
    const double angle = period > 0.0 ? phase * 2.0 * kPi / period : 0.0;
    return Motion{
        radius * std::cos(angle),
        radius * std::sin(angle),
        config.altitude_m,
        config.speed_mps,
        0.0F,
        normalize_heading(-angle * 180.0 / kPi),
        config.altitude_m > 0.0F,
    };
}

Motion sample_rectangle(const TrajectoryConfig &config, double seconds,
                        uint16_t instance_index) {
    const double width = config.primary_size_m;
    const double height = config.secondary_size_m;
    const double perimeter = 2.0 * (width + height);
    const double period = config.speed_mps > 0.0F ? perimeter / config.speed_mps : 0.0;
    double distance = positive_phase(seconds, period, instance_index) * config.speed_mps;
    Motion motion{};
    motion.altitude_m = config.altitude_m;
    motion.horizontal_speed_mps = config.speed_mps;
    motion.airborne = config.altitude_m > 0.0F;

    if (distance < width) {
        motion.east_m = -width / 2.0 + distance;
        motion.north_m = -height / 2.0;
        motion.heading_deg = 90.0F;
    } else if ((distance -= width) < height) {
        motion.east_m = width / 2.0;
        motion.north_m = -height / 2.0 + distance;
        motion.heading_deg = 0.0F;
    } else if ((distance -= height) < width) {
        motion.east_m = width / 2.0 - distance;
        motion.north_m = height / 2.0;
        motion.heading_deg = 270.0F;
    } else {
        distance -= width;
        motion.east_m = -width / 2.0;
        motion.north_m = height / 2.0 - distance;
        motion.heading_deg = 180.0F;
    }
    return motion;
}

Motion sample_waypoints(const TrajectoryConfig &config, double seconds,
                        uint16_t instance_index) {
    if (config.waypoints.empty()) return Motion{};
    if (config.waypoints.size() == 1 || config.speed_mps <= 0.0F) {
        const auto &point = config.waypoints.front();
        return Motion{point.east_m, point.north_m, point.up_m, 0.0F, 0.0F, 0.0F,
                      point.up_m > 0.0F};
    }

    double loop_length = 0.0;
    for (size_t i = 0; i < config.waypoints.size(); ++i) {
        const auto &from = config.waypoints[i];
        const auto &to = config.waypoints[(i + 1) % config.waypoints.size()];
        loop_length += std::hypot(static_cast<double>(to.east_m - from.east_m),
                                  static_cast<double>(to.north_m - from.north_m));
    }
    if (loop_length <= 0.0) {
        const auto &point = config.waypoints.front();
        return Motion{point.east_m, point.north_m, point.up_m, 0.0F, 0.0F, 0.0F,
                      point.up_m > 0.0F};
    }

    double distance =
        positive_phase(seconds, loop_length / config.speed_mps, instance_index) * config.speed_mps;
    for (size_t i = 0; i < config.waypoints.size(); ++i) {
        const auto &from = config.waypoints[i];
        const auto &to = config.waypoints[(i + 1) % config.waypoints.size()];
        const double east_delta = to.east_m - from.east_m;
        const double north_delta = to.north_m - from.north_m;
        const double segment_length = std::hypot(east_delta, north_delta);
        if (segment_length <= 0.0) continue;
        if (distance <= segment_length) {
            const double ratio = distance / segment_length;
            const float altitude =
                static_cast<float>(from.up_m + (to.up_m - from.up_m) * ratio);
            return Motion{
                from.east_m + east_delta * ratio,
                from.north_m + north_delta * ratio,
                altitude,
                config.speed_mps,
                static_cast<float>((to.up_m - from.up_m) * config.speed_mps / segment_length),
                normalize_heading(std::atan2(east_delta, north_delta) * 180.0 / kPi),
                altitude > 0.0F,
            };
        }
        distance -= segment_length;
    }
    const auto &point = config.waypoints.front();
    return Motion{point.east_m, point.north_m, point.up_m, config.speed_mps, 0.0F, 0.0F,
                  point.up_m > 0.0F};
}

Motion sample_mission(const TrajectoryConfig &config, double seconds,
                      uint16_t instance_index) {
    const double climb_time = config.vertical_speed_mps > 0.0F
                                  ? config.altitude_m / config.vertical_speed_mps
                                  : 0.0;
    const double cruise_time = config.cruise_time_ms / kMillisecondsPerSecond;
    const double period = 2.0 * climb_time + cruise_time;
    const double phase = positive_phase(seconds, period, instance_index);

    Motion motion{};
    if (phase <= 0.0 || period <= 0.0) return motion;
    if (phase < climb_time) {
        motion.altitude_m = static_cast<float>(phase * config.vertical_speed_mps);
        motion.vertical_speed_mps = config.vertical_speed_mps;
    } else if (phase < climb_time + cruise_time) {
        motion.altitude_m = config.altitude_m;
    } else {
        motion.altitude_m = static_cast<float>(
            config.altitude_m - (phase - climb_time - cruise_time) * config.vertical_speed_mps);
        motion.vertical_speed_mps = -config.vertical_speed_mps;
    }
    motion.airborne = motion.altitude_m > 0.0F;
    return motion;
}

}  // namespace

TrajectoryConfig hover_trajectory(float altitude_m) {
    TrajectoryConfig config;
    config.type = TrajectoryType::Hover;
    config.altitude_m = altitude_m;
    return config;
}

TrajectoryConfig line_trajectory(float length_m, float speed_mps, float heading_deg,
                                 float altitude_m) {
    TrajectoryConfig config;
    config.type = TrajectoryType::Line;
    config.primary_size_m = length_m;
    config.speed_mps = speed_mps;
    config.heading_deg = heading_deg;
    config.altitude_m = altitude_m;
    return config;
}

TrajectoryConfig circle_trajectory(float radius_m, float speed_mps, float altitude_m) {
    TrajectoryConfig config;
    config.type = TrajectoryType::Circle;
    config.primary_size_m = radius_m;
    config.speed_mps = speed_mps;
    config.altitude_m = altitude_m;
    return config;
}

TrajectoryConfig rectangle_trajectory(float width_m, float height_m, float speed_mps,
                                      float altitude_m) {
    TrajectoryConfig config;
    config.type = TrajectoryType::Rectangle;
    config.primary_size_m = width_m;
    config.secondary_size_m = height_m;
    config.speed_mps = speed_mps;
    config.altitude_m = altitude_m;
    return config;
}

TrajectoryConfig waypoint_trajectory(std::vector<EnuPoint> waypoints, float speed_mps) {
    TrajectoryConfig config;
    config.type = TrajectoryType::WaypointLoop;
    config.speed_mps = speed_mps;
    config.waypoints = std::move(waypoints);
    return config;
}

TrajectoryConfig takeoff_cruise_land_trajectory(float altitude_m, float vertical_speed_mps,
                                                uint32_t cruise_time_ms) {
    TrajectoryConfig config;
    config.type = TrajectoryType::TakeoffCruiseLand;
    config.altitude_m = altitude_m;
    config.vertical_speed_mps = vertical_speed_mps;
    config.cruise_time_ms = cruise_time_ms;
    return config;
}

bool valid(const TrajectoryConfig &config) {
    const auto finite_nonnegative = [](float value) {
        return std::isfinite(value) && value >= 0.0F;
    };
    const auto finite_positive = [](float value) {
        return std::isfinite(value) && value > 0.0F;
    };

    switch (config.type) {
        case TrajectoryType::Hover:
            return finite_nonnegative(config.altitude_m);
        case TrajectoryType::Line:
            return finite_positive(config.primary_size_m) && finite_positive(config.speed_mps) &&
                   finite_nonnegative(config.altitude_m) && std::isfinite(config.heading_deg) &&
                   config.heading_deg >= 0.0F && config.heading_deg < 360.0F;
        case TrajectoryType::Circle:
            return finite_positive(config.primary_size_m) && finite_positive(config.speed_mps) &&
                   finite_nonnegative(config.altitude_m);
        case TrajectoryType::Rectangle:
            return finite_positive(config.primary_size_m) &&
                   finite_positive(config.secondary_size_m) && finite_positive(config.speed_mps) &&
                   finite_nonnegative(config.altitude_m);
        case TrajectoryType::WaypointLoop: {
            if (!finite_positive(config.speed_mps) || config.waypoints.size() < 2) return false;
            bool has_horizontal_segment = false;
            for (size_t i = 0; i < config.waypoints.size(); ++i) {
                const auto &point = config.waypoints[i];
                if (!std::isfinite(point.east_m) || !std::isfinite(point.north_m) ||
                    !finite_nonnegative(point.up_m)) {
                    return false;
                }
                const auto &next = config.waypoints[(i + 1) % config.waypoints.size()];
                has_horizontal_segment = has_horizontal_segment || point.east_m != next.east_m ||
                                         point.north_m != next.north_m;
            }
            return has_horizontal_segment;
        }
        case TrajectoryType::TakeoffCruiseLand:
            return finite_positive(config.altitude_m) &&
                   finite_positive(config.vertical_speed_mps);
    }
    return false;
}

TrajectoryEngine::TrajectoryEngine(GeoPoint site_center) : site_center_(site_center) {}

uint64_t TrajectoryEngine::extend_elapsed(uint16_t instance_index, uint32_t elapsed_ms) const {
    const size_t slot = instance_index % last_elapsed_.size();
    if (!elapsed_initialized_[slot]) {
        elapsed_initialized_[slot] = true;
    } else if (elapsed_ms < last_elapsed_[slot] &&
               last_elapsed_[slot] - elapsed_ms > (1U << 31)) {
        wrap_offset_[slot] += (1ULL << 32);
    }
    last_elapsed_[slot] = elapsed_ms;
    return wrap_offset_[slot] + elapsed_ms;
}

FlightState TrajectoryEngine::sample(const TrajectoryConfig &config, uint16_t instance_index,
                                     uint32_t elapsed_ms) const {
    const double seconds = extend_elapsed(instance_index, elapsed_ms) / kMillisecondsPerSecond;
    Motion motion{};
    switch (config.type) {
        case TrajectoryType::Hover:
            motion.altitude_m = config.altitude_m;
            motion.airborne = config.altitude_m > 0.0F;
            break;
        case TrajectoryType::Line:
            motion = sample_line(config, seconds, instance_index);
            break;
        case TrajectoryType::Circle:
            motion = sample_circle(config, seconds, instance_index);
            break;
        case TrajectoryType::Rectangle:
            motion = sample_rectangle(config, seconds, instance_index);
            break;
        case TrajectoryType::WaypointLoop:
            motion = sample_waypoints(config, seconds, instance_index);
            break;
        case TrajectoryType::TakeoffCruiseLand:
            motion = sample_mission(config, seconds, instance_index);
            break;
    }

    const auto position = enu_to_wgs84(site_center_, motion.east_m, motion.north_m);
    return FlightState{
        position.latitude_deg,
        position.longitude_deg,
        motion.altitude_m,
        motion.altitude_m,
        motion.horizontal_speed_mps,
        motion.vertical_speed_mps,
        normalize_heading(motion.heading_deg),
        motion.airborne,
        elapsed_ms,
    };
}

}  // namespace rid
