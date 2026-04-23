#include "gps_kalman.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

// ============================================================
// Host tests for the alpha-beta Kalman position filter.
//
// We feed it fixed sequences of (t, lat, lon, speed) and assert
// on the filtered output.  No hardware, no FreeRTOS.
//
// Conversion aid: 1 degree of latitude ≈ 111 320 m.  We use this
// when reasoning about "how many metres of jitter".
// ============================================================

static constexpr double DEG_PER_M_LAT = 1.0 / 111320.0;

static GpsKalmanInput make_input(double lat, double lon, int64_t t_us, float speed_kmh) {
    GpsKalmanInput in = {};
    in.lat_deg = lat;
    in.lon_deg = lon;
    in.t_us = t_us;
    in.speed_kmh = speed_kmh;
    return in;
}

static void test_first_sample_is_passthrough() {
    GpsKalmanState state = {};
    gps_kalman_reset(&state);
    GpsKalmanOutput out = {};
    bool ok = gps_kalman_update(&state, make_input(50.0, 10.0, 0, 0.0f), &out);
    assert(ok);
    // Uninitialized state should take the first sample verbatim.
    assert(out.lat_deg == 50.0);
    assert(out.lon_deg == 10.0);
    assert(state.initialized);
}

static void test_stationary_jitter_is_heavily_smoothed() {
    // Simulate the "board on the ground" case: true position is
    // fixed, but each sample has ±3 m of white noise.  After 50
    // samples the filter output should be much closer to the
    // truth than the raw samples.
    GpsKalmanState state = {};
    gps_kalman_reset(&state);

    const double truth_lat = 50.0;
    const double truth_lon = 10.0;

    // Pseudo-random but deterministic jitter sequence.  Amplitude
    // 3 m converted to degrees.
    double jitter[] = {
        +2.8, -2.1, +1.7, -2.9, +0.3,
        -1.2, +2.4, -0.8, +1.9, -2.6,
        +1.1, -1.4, +2.2, -1.8, +0.7,
        -2.3, +1.5, -0.6, +2.0, -1.9,
        +0.9, -2.5, +1.3, -0.4, +2.1,
        -1.7, +0.2, -1.1, +2.7, -2.4,
        +1.6, -0.9, +1.0, -2.2, +0.5,
        -1.5, +2.6, -0.3, +1.8, -2.0,
        +0.8, -1.6, +2.3, -0.7, +1.2,
        -2.8, +1.4, -0.5, +0.6, -2.7,
    };
    const int n = sizeof(jitter) / sizeof(jitter[0]);

    GpsKalmanOutput out = {};
    double max_err_m = 0.0;
    for (int i = 0; i < n; i++) {
        double lat = truth_lat + jitter[i] * DEG_PER_M_LAT;
        double lon = truth_lon;
        gps_kalman_update(&state,
                          make_input(lat, lon, static_cast<int64_t>(i) * 100000, 0.0f),
                          &out);
        if (i >= 10) {  // allow warmup
            double err_m = fabs(out.lat_deg - truth_lat) / DEG_PER_M_LAT;
            if (err_m > max_err_m) max_err_m = err_m;
        }
    }

    // Raw jitter is up to 2.9 m.  After the filter we want max
    // error under 1 m — that's the "jitter kill" criterion.
    assert(max_err_m < 1.0);
}

static void test_steady_motion_tracks_without_lag() {
    // Vehicle moving north at constant 10 m/s for 5 s, sampled at
    // 10 Hz (dt = 0.1 s).  After warmup, filter output should
    // track within ~1 m of truth.  This verifies beta gain is
    // high enough to catch up to real motion.
    GpsKalmanState state = {};
    gps_kalman_reset(&state);

    const double start_lat = 50.0;
    const double v_mps     = 10.0;  // 36 km/h
    const double v_deg_per_s = v_mps * DEG_PER_M_LAT;
    const double dt_s      = 0.1;

    GpsKalmanOutput out = {};
    const int n_samples = 50;
    double final_err_m = 0.0;
    for (int i = 0; i < n_samples; i++) {
        double t_s = static_cast<double>(i) * dt_s;
        double truth_lat = start_lat + v_deg_per_s * t_s;
        // No noise for this test — pure tracking.
        int64_t t_us = static_cast<int64_t>(t_s * 1000000.0);
        gps_kalman_update(&state,
                          make_input(truth_lat, 10.0, t_us, 36.0f),
                          &out);
        if (i == n_samples - 1) {
            final_err_m = fabs(out.lat_deg - truth_lat) / DEG_PER_M_LAT;
        }
    }

    // After 5 s of steady motion, tracking error should be under 1 m.
    assert(final_err_m < 1.0);
}

static void test_large_time_gap_forces_reseed() {
    // After a 5 s gap the CV model would extrapolate far out of
    // reality, so the filter should re-seed from the new sample
    // rather than try to coast across the gap.
    GpsKalmanState state = {};
    gps_kalman_reset(&state);

    GpsKalmanOutput out = {};
    gps_kalman_update(&state, make_input(50.0, 10.0, 0, 10.0f), &out);
    gps_kalman_update(&state, make_input(50.00001, 10.0, 100000, 10.0f), &out);
    // Now jump forward 5 s and 50 m — simulates signal recovery.
    double far_lat = 50.0 + 50.0 * DEG_PER_M_LAT;
    gps_kalman_update(&state,
                      make_input(far_lat, 10.0, 5100000, 10.0f),
                      &out);

    // After re-seed, output should be exactly the new sample
    // (not an extrapolated blend of stale state + new).
    assert(out.lat_deg == far_lat);
    assert(out.lon_deg == 10.0);
    // Velocity should have reset to zero, so a follow-up sample
    // 100 ms later at the same location should not extrapolate.
    gps_kalman_update(&state,
                      make_input(far_lat, 10.0, 5200000, 10.0f),
                      &out);
    assert(fabs(out.lat_deg - far_lat) < 1e-9);
}

static void test_reset_puts_state_back_to_seed_on_next_sample() {
    GpsKalmanState state = {};
    gps_kalman_reset(&state);
    GpsKalmanOutput out = {};
    gps_kalman_update(&state, make_input(50.0, 10.0, 0, 10.0f), &out);
    gps_kalman_update(&state, make_input(50.00001, 10.0, 100000, 10.0f), &out);

    gps_kalman_reset(&state);
    assert(!state.initialized);

    // Post-reset: first sample is passthrough.
    gps_kalman_update(&state, make_input(60.0, 20.0, 200000, 0.0f), &out);
    assert(out.lat_deg == 60.0);
    assert(out.lon_deg == 20.0);
}

static void test_null_arguments_return_false() {
    GpsKalmanState state = {};
    GpsKalmanOutput out = {};
    GpsKalmanInput in = make_input(50.0, 10.0, 0, 0.0f);
    assert(!gps_kalman_update(nullptr, in, &out));
    assert(!gps_kalman_update(&state, in, nullptr));
    // Should be a no-op when state is null.
    gps_kalman_reset(nullptr);
}

static void test_non_monotonic_timestamps_reseed() {
    // Clock going backwards (e.g. PPS wrap) should not explode the
    // filter.  It should re-seed cleanly.
    GpsKalmanState state = {};
    gps_kalman_reset(&state);
    GpsKalmanOutput out = {};
    gps_kalman_update(&state, make_input(50.0, 10.0, 1000000, 10.0f), &out);
    // Now a sample with earlier timestamp.
    gps_kalman_update(&state, make_input(50.1, 10.1, 500000, 10.0f), &out);
    // Post-reseed the output should equal the new sample.
    assert(out.lat_deg == 50.1);
    assert(out.lon_deg == 10.1);
}

int main() {
    test_first_sample_is_passthrough();
    test_stationary_jitter_is_heavily_smoothed();
    test_steady_motion_tracks_without_lag();
    test_large_time_gap_forces_reseed();
    test_reset_puts_state_back_to_seed_on_next_sample();
    test_null_arguments_return_false();
    test_non_monotonic_timestamps_reseed();
    printf("OK: gps_kalman\n");
    return 0;
}
