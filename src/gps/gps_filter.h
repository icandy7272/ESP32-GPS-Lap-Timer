#pragma once

#include <stdint.h>

enum GpsQualityTier : uint8_t {
    GPS_QUALITY_TIER_POOR = 0,
    GPS_QUALITY_TIER_FAIR = 1,
    GPS_QUALITY_TIER_GOOD = 2,
    GPS_QUALITY_TIER_EXCELLENT = 3,
};

struct GpsQualityInput {
    int   fix_quality;
    int   fix_type;
    int   satellites;
    float speed_kmh;
    float hdop;
    float pdop;
};

struct GpsQualityResult {
    uint8_t quality_score;
    uint8_t quality_tier;
    bool    heading_reliable;
    bool    fix_3d;
};

GpsQualityResult gps_filter_assess_quality(const GpsQualityInput& input);
