#include "gps/gps_filter.h"

namespace {

static constexpr float HEADING_RELIABLE_SPEED_KMH = 5.0f;

static int clamp_score(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

static bool has_position_fix(int fix_quality, int fix_type) {
    if (fix_type > 0) {
        return fix_type >= 2;
    }
    return fix_quality == 1 || fix_quality == 2 ||
           fix_quality == 4 || fix_quality == 5;
}

static bool has_3d_fix(int fix_quality, int fix_type, int satellites) {
    if (fix_type > 0) {
        return fix_type >= 3;
    }
    return has_position_fix(fix_quality, fix_type) && satellites >= 6;
}

static int score_fix_quality(int fix_quality) {
    switch (fix_quality) {
        case 4: return 34;  // RTK fixed
        case 5: return 32;  // RTK float
        case 2: return 28;  // DGPS
        case 1: return 22;  // Autonomous GPS
        default: return 0;
    }
}

static int score_fix_type(int fix_type) {
    if (fix_type >= 3) {
        return 20;
    }
    if (fix_type == 2) {
        return 6;
    }
    return 0;
}

static int score_satellites(int satellites) {
    if (satellites >= 15) return 18;
    if (satellites >= 12) return 15;
    if (satellites >= 9)  return 12;
    if (satellites >= 7)  return 8;
    if (satellites >= 5)  return 4;
    return 0;
}

static int score_hdop(float hdop) {
    if (hdop <= 0.0f) return 6;  // GSA absent; do not over-reward.
    if (hdop <= 0.9f) return 18;
    if (hdop <= 1.5f) return 15;
    if (hdop <= 2.5f) return 10;
    if (hdop <= 4.0f) return 4;
    return 0;
}

static int score_pdop(float pdop) {
    if (pdop <= 0.0f) return 4;
    if (pdop <= 1.5f) return 10;
    if (pdop <= 2.5f) return 8;
    if (pdop <= 4.0f) return 4;
    return 0;
}

static uint8_t tier_for_score(uint8_t score) {
    if (score >= 80) return GPS_QUALITY_TIER_EXCELLENT;
    if (score >= 60) return GPS_QUALITY_TIER_GOOD;
    if (score >= 40) return GPS_QUALITY_TIER_FAIR;
    return GPS_QUALITY_TIER_POOR;
}

}  // namespace

GpsQualityResult gps_filter_assess_quality(const GpsQualityInput& input) {
    GpsQualityResult result = {};

    if (!has_position_fix(input.fix_quality, input.fix_type)) {
        return result;
    }

    int score = score_fix_quality(input.fix_quality)
              + score_fix_type(input.fix_type)
              + score_satellites(input.satellites)
              + score_hdop(input.hdop)
              + score_pdop(input.pdop);

    result.fix_3d = has_3d_fix(input.fix_quality,
                               input.fix_type,
                               input.satellites);
    result.quality_score = static_cast<uint8_t>(clamp_score(score));
    result.quality_tier = tier_for_score(result.quality_score);
    result.heading_reliable = result.fix_3d
                           && input.speed_kmh >= HEADING_RELIABLE_SPEED_KMH
                           && result.quality_score >= 50;
    return result;
}
