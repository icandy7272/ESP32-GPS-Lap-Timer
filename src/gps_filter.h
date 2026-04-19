#pragma once

#include "types.h"

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

struct GpsFilterDiagnostics {
    uint32_t display_outlier_drops;
    uint32_t match_outlier_drops;
    uint32_t stationary_holds;
    uint32_t heading_freezes;
    // Increments each time the filter clears its own stale baseline
    // after too many consecutive rejects.  A growing counter means the
    // GPS receiver is producing legitimate large jumps the reject
    // threshold keeps catching — not a bug, but worth surfacing so
    // the operator knows "the filter had to reset under me".
    // Added for the 2026-04-19 follow-up (P1 freeze fix).
    uint32_t filter_resets;
};

struct GpsFilterProcessResult {
    GpsPoint raw_fix;
    GpsPoint display_fix;
    GpsPoint match_fix;
    bool     display_valid;
    bool     match_valid;
    bool     display_rejected;
    bool     match_rejected;
};

GpsQualityResult gps_filter_assess_quality(const GpsQualityInput& input);

void gps_filter_reset();
GpsFilterProcessResult gps_filter_process(const GpsPoint& raw_fix);
bool gps_filter_get_display_fix(GpsPoint* out);
bool gps_filter_get_match_fix(GpsPoint* out);
GpsFilterDiagnostics gps_filter_get_diagnostics();
