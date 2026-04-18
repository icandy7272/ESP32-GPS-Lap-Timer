#include "gps/gps_filter.h"

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#endif

#include <math.h>

#if defined(ARDUINO)
static portMUX_TYPE s_filter_mux = portMUX_INITIALIZER_UNLOCKED;
#define GPS_FILTER_LOCK() portENTER_CRITICAL(&s_filter_mux)
#define GPS_FILTER_UNLOCK() portEXIT_CRITICAL(&s_filter_mux)
#else
#define GPS_FILTER_LOCK()
#define GPS_FILTER_UNLOCK()
#endif

namespace {

static constexpr float HEADING_RELIABLE_SPEED_KMH = 5.0f;
static constexpr int   RAW_HISTORY_CAPACITY = 5;
static constexpr int   MEDIAN_WINDOW_MIN = 3;
static constexpr float DISPLAY_MEDIAN_MAX_SPEED_KMH = 8.0f;
static constexpr float STATIONARY_SPEED_MAX_KMH = 2.0f;
static constexpr float STATIONARY_HOLD_RADIUS_M = 1.0f;

struct GpsFilterState {
    bool display_valid;
    bool match_valid;
    GpsPoint display_fix;
    GpsPoint match_fix;
    GpsFilterDiagnostics diagnostics;
    GpsPoint raw_history[RAW_HISTORY_CAPACITY];
    int raw_history_count;
    int raw_history_next;
};

static GpsFilterState s_state = {};

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

static double deg_to_rad(double deg) {
    return deg * 0.017453292519943295;
}

static double approx_distance_m(double lat1_deg,
                                double lon1_deg,
                                double lat2_deg,
                                double lon2_deg) {
    double lat1 = deg_to_rad(lat1_deg);
    double lat2 = deg_to_rad(lat2_deg);
    double d_lat = lat2 - lat1;
    double d_lon = deg_to_rad(lon2_deg - lon1_deg);
    double x = d_lon * cos((lat1 + lat2) * 0.5);
    double y = d_lat;
    return 6371000.0 * sqrt((x * x) + (y * y));
}

static void sort_values(double* values, int count) {
    for (int i = 1; i < count; i++) {
        double key = values[i];
        int j = i - 1;
        while (j >= 0 && values[j] > key) {
            values[j + 1] = values[j];
            j--;
        }
        values[j + 1] = key;
    }
}

static float display_alpha_for_speed(float speed_kmh) {
    if (speed_kmh < 2.0f) return 0.18f;
    if (speed_kmh < 5.0f) return 0.24f;
    if (speed_kmh < 12.0f) return 0.34f;
    if (speed_kmh < 25.0f) return 0.48f;
    return 0.62f;
}

static float match_alpha_for_speed(float speed_kmh) {
    if (speed_kmh < 2.0f) return 0.35f;
    if (speed_kmh < 5.0f) return 0.42f;
    if (speed_kmh < 12.0f) return 0.55f;
    if (speed_kmh < 25.0f) return 0.68f;
    return 0.78f;
}

static double blend_double(double prev, double next, float alpha) {
    return prev + ((next - prev) * alpha);
}

static float blend_float(float prev, float next, float alpha) {
    return prev + ((next - prev) * alpha);
}

static void push_raw_history(const GpsPoint& raw_fix) {
    s_state.raw_history[s_state.raw_history_next] = raw_fix;
    s_state.raw_history_next =
        (s_state.raw_history_next + 1) % RAW_HISTORY_CAPACITY;
    if (s_state.raw_history_count < RAW_HISTORY_CAPACITY) {
        s_state.raw_history_count++;
    }
}

static int raw_history_start() {
    return (s_state.raw_history_next - s_state.raw_history_count
            + RAW_HISTORY_CAPACITY) % RAW_HISTORY_CAPACITY;
}

static GpsPoint raw_history_at(int offset_from_oldest) {
    int idx = (raw_history_start() + offset_from_oldest) % RAW_HISTORY_CAPACITY;
    return s_state.raw_history[idx];
}

static double median_recent_coord(bool latitude) {
    double values[RAW_HISTORY_CAPACITY] = {};
    int count = s_state.raw_history_count;

    for (int i = 0; i < count; i++) {
        GpsPoint point = raw_history_at(i);
        values[i] = latitude ? point.lat_deg : point.lon_deg;
    }

    sort_values(values, count);
    return values[count / 2];
}

static GpsPoint apply_display_median(const GpsPoint& raw_fix) {
    GpsPoint filtered = raw_fix;
    if (raw_fix.speed_kmh > DISPLAY_MEDIAN_MAX_SPEED_KMH ||
        s_state.raw_history_count < MEDIAN_WINDOW_MIN) {
        return filtered;
    }

    filtered.lat_deg = median_recent_coord(true);
    filtered.lon_deg = median_recent_coord(false);
    return filtered;
}

static void apply_heading_freeze(GpsPoint* fix,
                                 const GpsPoint& previous_fix) {
    if (fix->heading_reliable &&
        fix->speed_kmh >= HEADING_RELIABLE_SPEED_KMH) {
        return;
    }

    fix->heading_deg = previous_fix.heading_deg;
    s_state.diagnostics.heading_freezes++;
}

static void apply_stationary_hold(GpsPoint* fix,
                                  const GpsPoint& previous_fix) {
    if (fix->speed_kmh > STATIONARY_SPEED_MAX_KMH) {
        return;
    }

    double step_m = approx_distance_m(previous_fix.lat_deg, previous_fix.lon_deg,
                                      fix->lat_deg, fix->lon_deg);
    if (step_m > STATIONARY_HOLD_RADIUS_M) {
        return;
    }

    fix->lat_deg = previous_fix.lat_deg;
    fix->lon_deg = previous_fix.lon_deg;
    fix->speed_kmh = 0.0f;
    s_state.diagnostics.stationary_holds++;
}

static GpsPoint apply_ema(const GpsPoint& previous_fix,
                          const GpsPoint& candidate,
                          float alpha) {
    GpsPoint filtered = candidate;
    filtered.lat_deg = blend_double(previous_fix.lat_deg,
                                    candidate.lat_deg,
                                    alpha);
    filtered.lon_deg = blend_double(previous_fix.lon_deg,
                                    candidate.lon_deg,
                                    alpha);
    filtered.speed_kmh = blend_float(previous_fix.speed_kmh,
                                     candidate.speed_kmh,
                                     alpha);
    return filtered;
}

static void store_filter_outputs(const GpsFilterProcessResult& result) {
    GPS_FILTER_LOCK();
    s_state.display_valid = result.display_valid;
    s_state.match_valid = result.match_valid;
    s_state.display_fix = result.display_fix;
    s_state.match_fix = result.match_fix;
    GPS_FILTER_UNLOCK();
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

void gps_filter_reset() {
    GPS_FILTER_LOCK();
    s_state = {};
    GPS_FILTER_UNLOCK();
}

GpsFilterProcessResult gps_filter_process(const GpsPoint& raw_fix) {
    GpsFilterProcessResult result = {};
    push_raw_history(raw_fix);

    result.raw_fix = raw_fix;
    result.display_valid = true;
    result.match_valid = true;

    GpsPoint display_fix = apply_display_median(raw_fix);
    GpsPoint match_fix = raw_fix;

    if (s_state.display_valid) {
        apply_stationary_hold(&display_fix, s_state.display_fix);
        display_fix = apply_ema(s_state.display_fix,
                                display_fix,
                                display_alpha_for_speed(raw_fix.speed_kmh));
        apply_heading_freeze(&display_fix, s_state.display_fix);
    }

    if (s_state.match_valid) {
        match_fix = apply_ema(s_state.match_fix,
                              match_fix,
                              match_alpha_for_speed(raw_fix.speed_kmh));
        apply_heading_freeze(&match_fix, s_state.match_fix);
    }

    result.display_fix = display_fix;
    result.match_fix = match_fix;

    store_filter_outputs(result);
    return result;
}

bool gps_filter_get_display_fix(GpsPoint* out) {
    if (out == nullptr) {
        return false;
    }

    GPS_FILTER_LOCK();
    bool valid = s_state.display_valid;
    if (valid) {
        *out = s_state.display_fix;
    }
    GPS_FILTER_UNLOCK();
    return valid;
}

bool gps_filter_get_match_fix(GpsPoint* out) {
    if (out == nullptr) {
        return false;
    }

    GPS_FILTER_LOCK();
    bool valid = s_state.match_valid;
    if (valid) {
        *out = s_state.match_fix;
    }
    GPS_FILTER_UNLOCK();
    return valid;
}

GpsFilterDiagnostics gps_filter_get_diagnostics() {
    GPS_FILTER_LOCK();
    GpsFilterDiagnostics diagnostics = s_state.diagnostics;
    GPS_FILTER_UNLOCK();
    return diagnostics;
}
