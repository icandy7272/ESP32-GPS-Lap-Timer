#include "gps_kalman.h"

#include <math.h>

namespace {

// ============================================================
// Gain tables.
//
// alpha — how much of the position residual to absorb into the
//         position estimate per step (0 = stuck, 1 = raw).
// beta  — how much of the position residual (scaled by 1/dt) to
//         absorb into the velocity estimate.
//
// Values are speed-tiered because the "right" amount of smoothing
// depends on how fast the vehicle is actually moving:
//
//   < 2 km/h  (stationary):  kill jitter as hard as possible.
//                            alpha=0.08 means a raw sample with
//                            3 m of noise nudges the filter by
//                            only 24 cm per fix — invisible on a
//                            10-m-per-grid live map.  Velocity
//                            gain is tiny because we believe
//                            truly-stationary and don't want
//                            random acceleration build-up.
//   2-8 km/h (walking):       alpha=0.18 still smooths hard,
//                            but velocity gain is high enough
//                            that walking pace is tracked within
//                            ~3 samples (≈ 0.3 s at 10 Hz).
//   8-25 km/h (low-speed):   alpha=0.35.  GPS noise is the same
//                            ~3 m but motion is 3-10× faster, so
//                            the signal-to-noise ratio flips and
//                            we should trust the measurement more.
//   ≥ 25 km/h (track):        alpha=0.55.  At kart-racing speeds
//                            the receiver's own smoothing matters
//                            much more than ours; we just need
//                            to avoid injecting latency.
//
// Beta is derived from alpha using Benedict-Bordner's critically
// damped choice:  beta = alpha^2 / (2 - alpha).  That removes the
// main failure mode of naive alpha-beta, which is velocity
// overshoot on a step input (e.g. kart sets off from a standstill).
// ============================================================

struct GainPair {
    double alpha;
    double beta;
};

static GainPair gains_for_speed(float speed_kmh) {
    double alpha;
    if (speed_kmh < 2.0f)       alpha = 0.08;
    else if (speed_kmh < 8.0f)  alpha = 0.18;
    else if (speed_kmh < 25.0f) alpha = 0.35;
    else                        alpha = 0.55;
    double beta = (alpha * alpha) / (2.0 - alpha);
    GainPair g = {};
    g.alpha = alpha;
    g.beta  = beta;
    return g;
}

// Clamp dt to a window where the CV model is plausible.
//
// dt < 0.01 s  (> 100 Hz):  almost certainly a timestamp bug, not
//                           a real fix.  Clamp up so we don't
//                           divide by near-zero in beta/dt.
// dt > 2.0 s:              too long a gap for CV extrapolation to
//                           be meaningful; caller re-seeds.
//
// Returns dt in seconds, or 0.0 to signal "re-seed".
static double clamp_dt_seconds(int64_t last_t_us, int64_t now_t_us) {
    if (now_t_us <= last_t_us) {
        return 0.0;  // re-seed: timestamps rolled backwards or duplicate sample
    }
    int64_t d_us = now_t_us - last_t_us;
    double dt = static_cast<double>(d_us) / 1000000.0;
    if (dt > 2.0) {
        return 0.0;  // re-seed: gap too long for CV model
    }
    if (dt < 0.01) {
        dt = 0.01;
    }
    return dt;
}

static void seed(GpsKalmanState* state, const GpsKalmanInput& input) {
    state->initialized   = true;
    state->lat_deg       = input.lat_deg;
    state->lon_deg       = input.lon_deg;
    state->vlat_deg_per_s = 0.0;
    state->vlon_deg_per_s = 0.0;
    state->last_t_us     = input.t_us;
}

}  // namespace

void gps_kalman_reset(GpsKalmanState* state) {
    if (state == nullptr) {
        return;
    }
    // Zero-init all fields, including the initialized flag.
    *state = GpsKalmanState{};
}

bool gps_kalman_update(GpsKalmanState* state,
                       const GpsKalmanInput& input,
                       GpsKalmanOutput* output) {
    if (state == nullptr || output == nullptr) {
        return false;
    }

    // First measurement, or a previous re-seed requested by a gap.
    if (!state->initialized) {
        seed(state, input);
        output->lat_deg = input.lat_deg;
        output->lon_deg = input.lon_deg;
        return true;
    }

    double dt = clamp_dt_seconds(state->last_t_us, input.t_us);
    if (dt == 0.0) {
        // Gap too long or monotonicity broken — start fresh from
        // this sample rather than extrapolate across it.
        seed(state, input);
        output->lat_deg = input.lat_deg;
        output->lon_deg = input.lon_deg;
        return true;
    }

    GainPair g = gains_for_speed(input.speed_kmh);

    // Predict
    double lat_pred = state->lat_deg + state->vlat_deg_per_s * dt;
    double lon_pred = state->lon_deg + state->vlon_deg_per_s * dt;

    // Residual
    double r_lat = input.lat_deg - lat_pred;
    double r_lon = input.lon_deg - lon_pred;

    // Update
    state->lat_deg       = lat_pred + g.alpha * r_lat;
    state->lon_deg       = lon_pred + g.alpha * r_lon;
    state->vlat_deg_per_s = state->vlat_deg_per_s + (g.beta / dt) * r_lat;
    state->vlon_deg_per_s = state->vlon_deg_per_s + (g.beta / dt) * r_lon;
    state->last_t_us     = input.t_us;

    output->lat_deg = state->lat_deg;
    output->lon_deg = state->lon_deg;
    return true;
}
