#include "gps/gps_filter.h"

#include <assert.h>

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

int main() {
    test_high_quality_3d_fix_scores_well();
    test_low_speed_2d_fix_keeps_heading_unreliable();
    test_missing_gsa_falls_back_to_fix_quality_and_satellites();
    return 0;
}
