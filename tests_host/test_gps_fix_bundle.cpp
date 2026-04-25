#include "gps_fix_bundle.h"

#include <assert.h>

static GpsPoint make_point(double lat_deg,
                           double lon_deg,
                           int64_t timestamp_us) {
    GpsPoint point = {};
    point.lat_deg = lat_deg;
    point.lon_deg = lon_deg;
    point.timestamp_us = timestamp_us;
    point.fix_3d = true;
    point.satellites = 12;
    return point;
}

static void test_bundle_preserves_aligned_raw_and_match_fixes() {
    GpsFilterProcessResult result = {};
    result.raw_fix = make_point(31.100000, 121.200000, 1000000);
    result.match_fix = make_point(31.100010, 121.200020, 1000000);
    result.match_valid = true;

    GpsFixBundle bundle = gps_fix_bundle_from_filter_result(result);

    assert(bundle.raw_fix.lat_deg == result.raw_fix.lat_deg);
    assert(bundle.raw_fix.lon_deg == result.raw_fix.lon_deg);
    assert(bundle.match_fix.lat_deg == result.match_fix.lat_deg);
    assert(bundle.match_fix.lon_deg == result.match_fix.lon_deg);
    assert(bundle.match_valid);
}

static void test_rejected_match_still_carries_filter_match_fix() {
    GpsFilterProcessResult result = {};
    result.raw_fix = make_point(31.100000, 121.200000, 1040000);
    result.match_fix = make_point(31.099900, 121.199900, 1000000);
    result.match_valid = true;
    result.match_rejected = true;

    GpsFixBundle bundle = gps_fix_bundle_from_filter_result(result);

    assert(bundle.match_valid);
    assert(bundle.match_fix.lat_deg == result.match_fix.lat_deg);
    assert(bundle.match_fix.lon_deg == result.match_fix.lon_deg);
    assert(bundle.match_fix.lat_deg != result.raw_fix.lat_deg);
    assert(bundle.match_fix.lon_deg != result.raw_fix.lon_deg);
}

static void test_invalid_match_falls_back_to_raw_for_bundle_payload() {
    GpsFilterProcessResult result = {};
    result.raw_fix = make_point(31.100000, 121.200000, 1040000);
    result.match_valid = false;

    GpsFixBundle bundle = gps_fix_bundle_from_filter_result(result);

    assert(!bundle.match_valid);
    assert(bundle.match_fix.lat_deg == result.raw_fix.lat_deg);
    assert(bundle.match_fix.lon_deg == result.raw_fix.lon_deg);
}

int main() {
    test_bundle_preserves_aligned_raw_and_match_fixes();
    test_rejected_match_still_carries_filter_match_fix();
    test_invalid_match_falls_back_to_raw_for_bundle_payload();
    return 0;
}
