#include "gps_filter.h"

#include <assert.h>
#include <cmath>
#include <math.h>

static GpsPoint make_point(double lat_deg,
                           double lon_deg,
                           float speed_kmh,
                           float heading_deg,
                           int64_t timestamp_us) {
    GpsPoint point = {};
    point.lat_deg = lat_deg;
    point.lon_deg = lon_deg;
    point.speed_kmh = speed_kmh;
    point.heading_deg = heading_deg;
    point.height_m = 0.0f;
    point.satellites = 12;
    point.timestamp_us = timestamp_us;
    point.pps_synced = false;
    point.fix_3d = true;
    point.hdop = 0.9f;
    point.quality_score = 80;
    point.quality_tier = GPS_QUALITY_TIER_EXCELLENT;
    point.heading_reliable = true;
    return point;
}

static void test_high_quality_3d_fix_scores_well() {
    GpsQualityInput input = {};
    input.fix_quality = 4;
    input.fix_type = 3;
    input.satellites = 16;
    input.speed_kmh = 32.0f;
    input.hdop = 0.7f;
    input.pdop = 1.1f;

    GpsQualityResult result = gps_filter_assess_quality(input);

    assert(result.fix_3d);
    assert(result.heading_reliable);
    assert(result.quality_tier == GPS_QUALITY_TIER_EXCELLENT);
    assert(result.quality_score >= 80);
}

static void test_low_speed_2d_fix_keeps_heading_unreliable() {
    GpsQualityInput input = {};
    input.fix_quality = 1;
    input.fix_type = 2;
    input.satellites = 5;
    input.speed_kmh = 1.8f;
    input.hdop = 4.8f;
    input.pdop = 7.0f;

    GpsQualityResult result = gps_filter_assess_quality(input);

    assert(!result.fix_3d);
    assert(!result.heading_reliable);
    assert(result.quality_tier == GPS_QUALITY_TIER_POOR);
    assert(result.quality_score < 40);
}

static void test_missing_gsa_falls_back_to_fix_quality_and_satellites() {
    GpsQualityInput input = {};
    input.fix_quality = 2;
    input.fix_type = 0;
    input.satellites = 10;
    input.speed_kmh = 22.0f;
    input.hdop = -1.0f;
    input.pdop = -1.0f;

    GpsQualityResult result = gps_filter_assess_quality(input);

    assert(result.fix_3d);
    assert(result.heading_reliable);
    assert(result.quality_tier >= GPS_QUALITY_TIER_FAIR);
    assert(result.quality_score >= 50);
}

static void test_filter_tracks_separate_raw_and_display_paths() {
    gps_filter_reset();

    GpsPoint raw_a = make_point(31.100000, 121.200000, 18.0f, 90.0f, 1000000);
    GpsPoint raw_b = make_point(31.100010, 121.200020, 19.0f, 92.0f, 1040000);

    GpsFilterProcessResult out_a = gps_filter_process(raw_a);
    GpsFilterProcessResult out_b = gps_filter_process(raw_b);
    GpsPoint display_fix = {};

    assert(out_a.display_valid);
    assert(out_a.match_valid);
    assert(out_b.raw_fix.lat_deg == raw_b.lat_deg);
    assert(out_b.raw_fix.lon_deg == raw_b.lon_deg);
    assert(gps_filter_get_display_fix(&display_fix));
    assert(display_fix.lat_deg == out_b.display_fix.lat_deg);
    assert(display_fix.lon_deg == out_b.display_fix.lon_deg);
}

static void test_low_speed_jitter_is_smoothed_for_display_path() {
    gps_filter_reset();

    const double base_lat = 31.100000;
    const double base_lon = 121.200000;
    const GpsPoint samples[] = {
        make_point(base_lat + 0.000000, base_lon + 0.000000, 1.6f, 35.0f, 1000000),
        make_point(base_lat + 0.000008, base_lon - 0.000007, 1.5f, 210.0f, 1040000),
        make_point(base_lat - 0.000007, base_lon + 0.000006, 1.4f, 180.0f, 1080000),
        make_point(base_lat + 0.000006, base_lon - 0.000005, 1.7f, 300.0f, 1120000),
        make_point(base_lat - 0.000005, base_lon + 0.000004, 1.5f, 120.0f, 1160000),
    };

    GpsFilterProcessResult out = {};
    for (const GpsPoint& sample : samples) {
        GpsPoint adjusted = sample;
        adjusted.heading_reliable = false;
        out = gps_filter_process(adjusted);
    }

    assert(fabs(out.display_fix.lat_deg - base_lat) <
           fabs(samples[4].lat_deg - base_lat));
    assert(fabs(out.display_fix.lon_deg - base_lon) <
           fabs(samples[4].lon_deg - base_lon));
}

static void test_low_speed_unreliable_heading_is_frozen() {
    gps_filter_reset();

    GpsPoint first = make_point(31.100000, 121.200000, 2.0f, 45.0f, 1000000);
    first.heading_reliable = false;
    GpsPoint second = make_point(31.100002, 121.200001, 1.8f, 220.0f, 1040000);
    second.heading_reliable = false;

    GpsFilterProcessResult out_first = gps_filter_process(first);
    GpsFilterProcessResult out_second = gps_filter_process(second);

    assert(out_first.display_fix.heading_deg == 45.0f);
    assert(out_second.display_fix.heading_deg == 45.0f);
}

static void test_implausible_position_jump_is_rejected() {
    gps_filter_reset();

    GpsPoint first = make_point(31.100000, 121.200000, 22.0f, 88.0f, 1000000);
    GpsPoint jump = make_point(31.110000, 121.210000, 24.0f, 90.0f, 1040000);

    GpsFilterProcessResult out_first = gps_filter_process(first);
    GpsFilterProcessResult out_jump = gps_filter_process(jump);
    GpsFilterDiagnostics diagnostics = gps_filter_get_diagnostics();

    assert(!out_first.display_rejected);
    assert(out_jump.display_rejected);
    assert(out_jump.match_rejected);
    assert(out_jump.raw_fix.lat_deg == jump.lat_deg);
    assert(out_jump.display_fix.lat_deg == out_first.display_fix.lat_deg);
    assert(diagnostics.display_outlier_drops >= 1);
    assert(diagnostics.match_outlier_drops >= 1);
}

static void test_implausible_acceleration_is_rejected() {
    gps_filter_reset();

    GpsPoint first = make_point(31.100000, 121.200000, 8.0f, 15.0f, 1000000);
    GpsPoint spike = make_point(31.100001, 121.200001, 85.0f, 16.0f, 1040000);

    GpsFilterProcessResult out_first = gps_filter_process(first);
    GpsFilterProcessResult out_spike = gps_filter_process(spike);

    assert(!out_first.display_rejected);
    assert(out_spike.display_rejected);
    assert(out_spike.match_rejected);
    assert(out_spike.display_fix.speed_kmh == out_first.display_fix.speed_kmh);
}

static void test_low_speed_heading_flip_is_rejected() {
    gps_filter_reset();

    GpsPoint first = make_point(31.100000, 121.200000, 4.0f, 15.0f, 1000000);
    GpsPoint flip = make_point(31.100001, 121.200001, 4.2f, 210.0f, 1040000);

    GpsFilterProcessResult out_first = gps_filter_process(first);
    GpsFilterProcessResult out_flip = gps_filter_process(flip);

    assert(!out_first.display_rejected);
    assert(out_flip.display_rejected);
    assert(out_flip.match_rejected);
}

// --- 2026-04-19 codex P1 follow-up: stale-baseline freeze ---------
//
// A legitimate GPS jump (RTK acquisition, tunnel exit, cold-start
// bias) must not trap the filter into rejecting every subsequent
// sample forever, because the reject test keeps comparing them to
// the same pre-jump baseline.  After MAX_CONSECUTIVE_REJECTS
// rejects in a row the filter should discard its stale baseline
// and accept the next sample, rebuilding display/match state from
// scratch.
static void test_consecutive_rejects_trigger_filter_reset() {
    gps_filter_reset();

    // Anchor a normal baseline.
    GpsPoint anchor = make_point(31.100000, 121.200000, 22.0f, 90.0f, 1000000);
    GpsFilterProcessResult out_anchor = gps_filter_process(anchor);
    assert(!out_anchor.display_rejected);

    // Feed MAX_CONSECUTIVE_REJECTS (5) implausible jumps.  Each sample
    // jumps ~1 km from anchor in 40 ms — every one must reject at
    // speed + acceleration thresholds.  Expect display_fix to stay
    // frozen on anchor for all 5.
    GpsPoint jump = make_point(31.110000, 121.210000, 24.0f, 92.0f, 1040000);
    for (int i = 0; i < 5; i++) {
        jump.timestamp_us = 1040000 + static_cast<int64_t>(i) * 40000;
        GpsFilterProcessResult r = gps_filter_process(jump);
        assert(r.display_rejected);
        assert(r.match_rejected);
        // Display stays on the original anchor while the filter keeps
        // rejecting.
        assert(std::fabs(r.display_fix.lat_deg - out_anchor.display_fix.lat_deg) < 1e-9);
    }

    // The 6th sample must be accepted via the escape hatch — the
    // stale baseline has been discarded, and this sample becomes the
    // new first-after-reset anchor.  display_fix should now move to
    // the jump coordinates (or close to them, via median) instead of
    // staying frozen on the original anchor.
    jump.timestamp_us = 1040000 + 5 * 40000;
    GpsFilterProcessResult r = gps_filter_process(jump);
    assert(!r.display_rejected);
    assert(!r.match_rejected);
    // Display has moved off the stale anchor.
    assert(std::fabs(r.display_fix.lat_deg - out_anchor.display_fix.lat_deg) > 1e-6);

    GpsFilterDiagnostics diag = gps_filter_get_diagnostics();
    assert(diag.filter_resets >= 1);
}

// --- 2026-04-19 codex P2 follow-up: reject pollution ---------------
//
// The display-median history must NOT contain samples the filter
// rejected.  Otherwise a cluster of bad fixes still shifts the
// median toward the exact outliers the reject pass was meant to
// hide.
static void test_rejected_sample_not_in_display_median_history() {
    gps_filter_reset();

    // Seed a stable low-speed baseline (speed < DISPLAY_MEDIAN_MAX_SPEED_KMH
    // so apply_display_median engages).  Low-speed jitter smoothing is
    // the code path under test.
    for (int i = 0; i < 6; i++) {
        GpsPoint p = make_point(31.100000, 121.200000, 1.0f, 0.0f,
                                1000000 + static_cast<int64_t>(i) * 100000);
        GpsFilterProcessResult r = gps_filter_process(p);
        assert(!r.display_rejected);
    }

    // Record the stable display lat/lon.
    GpsFilterProcessResult stable;
    stable = gps_filter_process(make_point(31.100000, 121.200000, 1.0f, 0.0f, 1700000));
    const double stable_lat = stable.display_fix.lat_deg;

    // Now inject one implausible spike that will be rejected by the
    // acceleration / speed limits (85 km/h in 40 ms from a 1 km/h
    // baseline).  Before the fix, this would still land in
    // raw_history and shift the subsequent median.
    GpsPoint spike = make_point(31.100000, 121.200000, 85.0f, 0.0f, 1740000);
    GpsFilterProcessResult r_spike = gps_filter_process(spike);
    assert(r_spike.display_rejected);

    // Feed a normal low-speed sample.  If the rejected spike had
    // leaked into raw_history, the median would shift toward the
    // spike's influence.  The fix gates push_raw_history on
    // !reject_display, so the median stays pinned to the 6 earlier
    // stable samples.
    GpsPoint next = make_point(31.100000, 121.200000, 1.0f, 0.0f, 1840000);
    GpsFilterProcessResult r_next = gps_filter_process(next);

    assert(!r_next.display_rejected);
    // Display median must still be the stable lat (spike did not
    // pollute the median input).
    assert(std::fabs(r_next.display_fix.lat_deg - stable_lat) < 1e-9);
}

// --- 2026-04-23 codex review finding 1 (HIGH) -----------------------
//
// The escape hatch used to only clear display_valid / match_valid.
// With Kalman now holding its own position + velocity state, and
// raw_history feeding the low-speed display median, "first-after-
// reset" became a lie: a post-reset accepted sample at low speed
// could still be pulled by the stale pre-jump Kalman velocity
// estimate, and any surviving raw_history entries could bias the
// median toward the pre-jump location.
//
// The fix also calls gps_kalman_reset() and zeroes raw_history_count
// / raw_history_next inside the hatch.  This test proves the reset
// by building up a Kalman velocity estimate in phase 1, forcing the
// hatch to fire in phase 2, and then verifying that a follow-up
// sample at the hatched position stays put instead of drifting in
// the phase-1 velocity direction.
static void test_low_speed_recovery_after_reset_reseeds_clean() {
    gps_filter_reset();

    // Phase 1: 6 walking-pace samples moving steadily northward.
    // Step is 1e-6 deg of latitude per 100 ms ≈ 0.11 m/sample ≈
    // 4 km/h — consistent with the reported speed so no reject
    // rule fires.  Over 6 samples Kalman builds up a non-trivial
    // northward velocity estimate; that's the "velocity memory"
    // we want to verify gets wiped by the escape hatch.
    for (int i = 0; i < 6; i++) {
        double lat = 31.100000 + 0.000001 * i;
        GpsPoint p = make_point(lat, 121.200000, 4.0f, 0.0f,
                                1000000 + static_cast<int64_t>(i) * 100000);
        GpsFilterProcessResult r = gps_filter_process(p);
        assert(!r.display_rejected);
    }

    // Phase 2: 6 implausible-jump samples at a completely different
    // location.  Each rejects on the implied-speed rule (the
    // position step is huge).  Reported speed is kept close to
    // phase 1's 4 km/h so that the follow-up phase-3 sample does
    // not itself trip the acceleration reject — we want to isolate
    // the escape-hatch + reset behavior from the reject rules.
    //
    // The first 5 get rejected (raising consecutive_rejects to
    // MAX=5), and the 6th triggers the escape hatch — which
    // clears display/match valid, resets kalman_display, zeroes
    // raw_history, and accepts the hatched sample as the fresh seed.
    const double hatch_lat = 31.200000;
    const double hatch_lon = 121.300000;
    for (int i = 0; i < 6; i++) {
        GpsPoint bad = make_point(hatch_lat, hatch_lon, 4.0f, 0.0f,
                                  1600000 + static_cast<int64_t>(i) * 40000);
        GpsFilterProcessResult r = gps_filter_process(bad);
        if (i < 5) {
            assert(r.display_rejected);
        } else {
            // Hatch fires on this iteration — sample is accepted as
            // the new seed.
            assert(!r.display_rejected);
            // First seeded sample passes through Kalman unchanged.
            assert(std::fabs(r.display_fix.lat_deg - hatch_lat) < 1e-9);
            assert(std::fabs(r.display_fix.lon_deg - hatch_lon) < 1e-9);
        }
    }

    GpsFilterDiagnostics diag = gps_filter_get_diagnostics();
    assert(diag.filter_resets >= 1);

    // Phase 3: send a follow-up low-speed sample at exactly the
    // hatched location.  If kalman_display was properly reset, its
    // velocity is zero and predicting forward by dt keeps the
    // position at hatch_lat.  Residual is zero, output is hatch_lat.
    //
    // If the old bug were still present (hatch doesn't reset
    // Kalman), the Kalman state would still hold ~1e-5 deg/s
    // northward velocity from phase 1, so after a 100 ms step it
    // would predict hatch_lat + 1e-6.  Residual against the
    // measurement would be -1e-6, alpha=0.18 would pull output
    // back by ~0.18e-6, leaving output at ~hatch_lat + 0.82e-6.
    // The 1e-8 assertion below distinguishes these cases.
    GpsPoint follow = make_point(hatch_lat, hatch_lon, 1.0f, 0.0f, 1900000);
    GpsFilterProcessResult r = gps_filter_process(follow);

    assert(!r.display_rejected);
    double lat_err_deg = std::fabs(r.display_fix.lat_deg - hatch_lat);
    double lon_err_deg = std::fabs(r.display_fix.lon_deg - hatch_lon);
    assert(lat_err_deg < 1e-8);
    assert(lon_err_deg < 1e-8);
}

int main() {
    test_high_quality_3d_fix_scores_well();
    test_low_speed_2d_fix_keeps_heading_unreliable();
    test_missing_gsa_falls_back_to_fix_quality_and_satellites();
    test_filter_tracks_separate_raw_and_display_paths();
    test_low_speed_jitter_is_smoothed_for_display_path();
    test_low_speed_unreliable_heading_is_frozen();
    test_implausible_position_jump_is_rejected();
    test_implausible_acceleration_is_rejected();
    test_low_speed_heading_flip_is_rejected();
    test_consecutive_rejects_trigger_filter_reset();
    test_rejected_sample_not_in_display_median_history();
    test_low_speed_recovery_after_reset_reseeds_clean();
    return 0;
}
