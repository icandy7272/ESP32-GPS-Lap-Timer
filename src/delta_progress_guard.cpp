#include "delta_progress_guard.h"

#include <math.h>

namespace delta_progress_guard {
namespace {

static constexpr double KMH_TO_MPS = 1.0 / 3.6;
static constexpr double WRAP_THRESHOLD = 0.75;
static constexpr double MIN_FORWARD_SLACK_M = 15.0;
static constexpr double BACKWARD_SLACK_M = 8.0;
static constexpr double SPEED_DISTANCE_MULTIPLIER = 3.0;
static constexpr double FIXED_FORWARD_MARGIN_M = 4.0;

static double clamp01(double value) {
    if (value < 0.0) return 0.0;
    if (value > 1.0) return 1.0;
    return value;
}

static double max_forward_step_m(float speed_kmh, double dt_s) {
    double speed_mps = speed_kmh > 0.0f
                     ? static_cast<double>(speed_kmh) * KMH_TO_MPS
                     : 0.0;
    double speed_budget = speed_mps * dt_s * SPEED_DISTANCE_MULTIPLIER
                        + FIXED_FORWARD_MARGIN_M;
    return speed_budget > MIN_FORWARD_SLACK_M
         ? speed_budget
         : MIN_FORWARD_SLACK_M;
}

}  // namespace

bool is_progress_plausible(double last_progress,
                           double candidate_progress,
                           double total_dist_m,
                           float speed_kmh,
                           double dt_s) {
    if (last_progress < 0.0 || total_dist_m <= 0.0 || dt_s <= 0.0) {
        return true;
    }

    double last = clamp01(last_progress);
    double cand = clamp01(candidate_progress);
    double raw_delta = cand - last;

    double forward_delta = raw_delta;
    if (raw_delta < -WRAP_THRESHOLD && last > WRAP_THRESHOLD
        && cand < (1.0 - WRAP_THRESHOLD)) {
        forward_delta = (1.0 - last) + cand;
    }

    if (forward_delta >= 0.0) {
        double forward_m = forward_delta * total_dist_m;
        return forward_m <= max_forward_step_m(speed_kmh, dt_s);
    }

    double backward_m = -forward_delta * total_dist_m;
    return backward_m <= BACKWARD_SLACK_M;
}

}  // namespace delta_progress_guard
