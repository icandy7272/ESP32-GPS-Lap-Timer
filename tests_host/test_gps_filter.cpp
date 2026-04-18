#include "gps/gps_filter.h"

#include <assert.h>
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
    return 0;
}
