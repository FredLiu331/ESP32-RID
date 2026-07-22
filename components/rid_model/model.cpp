#include "rid/model.hpp"

#include <cmath>

namespace rid {

bool valid(const FlightState &state) {
    return std::isfinite(state.latitude_deg) && state.latitude_deg >= -90.0 &&
           state.latitude_deg <= 90.0 && std::isfinite(state.longitude_deg) &&
           state.longitude_deg >= -180.0 && state.longitude_deg <= 180.0 &&
           std::isfinite(state.altitude_msl_m) && std::isfinite(state.height_agl_m) &&
           std::isfinite(state.horizontal_speed_mps) && state.horizontal_speed_mps >= 0.0F &&
           std::isfinite(state.vertical_speed_mps) && std::isfinite(state.heading_deg) &&
           state.heading_deg >= 0.0F && state.heading_deg < 360.0F;
}

bool valid_period_ms(int64_t period_ms) {
    return period_ms > 0;
}

}  // namespace rid
