#pragma once

// ============================================================
// Alpha-beta (constant-velocity Kalman-lite) filter for GPS
// position.  Pure module — no Arduino, no FreeRTOS, no Serial —
// so the math can be exercised from tests_host/.
//
// Why alpha-beta and not full Kalman?
//   - Two scalar gains per axis per step instead of a 4x4
//     covariance matrix update.  Runs in ~30 ns on ESP32-S3.
//   - Numerically stable — no matrix inverses, no subtraction
//     of similar quantities.
//   - Gains are the tuning knobs; they're speed-adaptive so the
//     filter smooths hard when stationary (kills GPS jitter) and
//     responds fast when actually moving (no visible lag).
//
// Why this is better than the existing gps_filter EMA:
//   - EMA blends positions only.  When the receiver is sitting
//     still, every noisy sample pushes the EMA by alpha * noise,
//     so the filter output jitters at ~alpha * sigma_pos.  That
//     is exactly what the user saw on live_map with the board on
//     the ground.
//   - Alpha-beta tracks velocity explicitly.  When velocity is
//     near zero and the measurement-prediction residual is small,
//     the filter pulls the position estimate toward zero-velocity
//     instead of toward the noisy sample.  Stationary jitter
//     collapses.
//   - When real motion starts, the velocity state catches up
//     within 2-3 samples (beta gain does the lifting), so the
//     filtered trajectory does not lag like EMA-only would at
//     equivalent stationary smoothing.
//
// State vector (per axis, lat and lon filtered independently):
//   x      = position, degrees
//   v      = velocity, degrees/second
//
// Measurement:
//   z      = GPS position, degrees
//
// Predict:  x_pred = x + v * dt;   v_pred = v
// Residual: r      = z - x_pred
// Update:   x      = x_pred + alpha * r
//           v      = v_pred + (beta / dt) * r
//
// Gain pair (alpha, beta) is picked from the reported speed tier.
// Beta = alpha^2 / (2 - alpha) is the Benedict-Bordner critically
// damped choice — no overshoot on step inputs.
// ============================================================

#include <stdbool.h>
#include <stdint.h>

struct GpsKalmanState {
    bool    initialized;
    double  lat_deg;          // filtered latitude
    double  lon_deg;          // filtered longitude
    double  vlat_deg_per_s;   // filtered latitudinal velocity
    double  vlon_deg_per_s;   // filtered longitudinal velocity
    int64_t last_t_us;        // timestamp of last accepted measurement
};

struct GpsKalmanInput {
    double  lat_deg;
    double  lon_deg;
    int64_t t_us;
    float   speed_kmh;   // used only for gain-tier selection
};

struct GpsKalmanOutput {
    double lat_deg;
    double lon_deg;
};

// Reset the filter.  Next update() call seeds state from its
// measurement and returns it unchanged.
void gps_kalman_reset(GpsKalmanState* state);

// Feed a measurement, get a filtered position back.
//
// Returns true on success, false if state or output is null.
//
// First call (or first call after reset / gap) seeds the filter
// and returns the raw measurement unchanged.  After that, each
// call advances the CV model by the elapsed dt and applies the
// alpha-beta update.
//
// dt is clamped to [0.01 s, 2.0 s].  A dt > 2 s or a non-positive
// dt forces a re-seed rather than trying to extrapolate across a
// big gap — that's where a CV model would produce the worst
// artifacts.
bool gps_kalman_update(GpsKalmanState* state,
                       const GpsKalmanInput& input,
                       GpsKalmanOutput* output);
