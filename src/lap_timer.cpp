// ============================================================
// Lap Timer Implementation — ESP32-S3 GPS Lap Timer
// Public API and shared module state.
// ============================================================

#include "lap_timer.h"
#include "lap_timer/lap_timer_internal.h"

#include "delta.h"

#include <Arduino.h>
#include <math.h>
#include <stdlib.h>  // ps_malloc

namespace lap_timer_internal {

QueueHandle_t s_gps_queue = nullptr;
QueueHandle_t s_vbo_queue = nullptr;
QueueHandle_t s_lap_event_queue = nullptr;
SemaphoreHandle_t s_session_mutex = nullptr;

TrackDefinition s_track_shadow = {};
volatile uint32_t s_active_track_version = 0;
const TrackDefinition* s_track = &s_track_shadow;

void lap_timer_bump_active_track_version_locked() {
    s_active_track_version++;
}

GpsPoint s_history[SPLINE_HISTORY];
int s_history_count = 0;

double s_arm_distance[MAX_SECTORS];
bool s_arm_ready[MAX_SECTORS];

int s_debounce_remaining[MAX_SECTORS];
bool s_debounce_active[MAX_SECTORS];
int64_t s_debounce_crossing_us[MAX_SECTORS];
double s_debounce_expected_sign[MAX_SECTORS];

bool s_crossing_candidate_active[MAX_SECTORS];
GpsPoint s_crossing_candidate_p0[MAX_SECTORS];
GpsPoint s_crossing_candidate_p1[MAX_SECTORS];
GpsPoint s_crossing_candidate_p2[MAX_SECTORS];
double s_crossing_candidate_expected_sign[MAX_SECTORS];

int s_current_sector = 0;
int64_t s_lap_start_us = 0;
int64_t s_sector_start_us = 0;
int32_t s_best_lap_time_ms = -1;
bool s_first_crossing = true;

GpsPoint* s_lap_points = nullptr;
int s_lap_point_count = 0;

void history_push(const GpsPoint* pt) {
    if (s_history_count < SPLINE_HISTORY) {
        s_history[s_history_count] = *pt;
        s_history_count++;
        return;
    }

    for (int i = 0; i < SPLINE_HISTORY - 1; i++) {
        s_history[i] = s_history[i + 1];
    }
    s_history[SPLINE_HISTORY - 1] = *pt;
}

const GpsPoint* history_get(int index) {
    return &s_history[index];
}

}  // namespace lap_timer_internal

static const double DEG_TO_RAD_D = M_PI / 180.0;
static const double EARTH_RADIUS_M = 6371000.0;

double haversine_m(double lat1, double lon1, double lat2, double lon2) {
    double dlat = (lat2 - lat1) * DEG_TO_RAD_D;
    double dlon = (lon2 - lon1) * DEG_TO_RAD_D;
    double a = sin(dlat / 2.0) * sin(dlat / 2.0)
             + cos(lat1 * DEG_TO_RAD_D) * cos(lat2 * DEG_TO_RAD_D)
             * sin(dlon / 2.0) * sin(dlon / 2.0);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return EARTH_RADIUS_M * c;
}

double cross_product_2d(double ax, double ay, double bx, double by) {
    return ax * by - ay * bx;
}

double point_to_segment_distance(double px, double py,
                                 double ax, double ay,
                                 double bx, double by,
                                 double* out_t) {
    double dx = bx - ax;
    double dy = by - ay;
    double len_sq = dx * dx + dy * dy;

    double t = 0.0;
    if (len_sq > 1e-12) {
        t = ((px - ax) * dx + (py - ay) * dy) / len_sq;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
    }
    if (out_t != nullptr) {
        *out_t = t;
    }

    double proj_x = ax + t * dx;
    double proj_y = ay + t * dy;
    double ex = px - proj_x;
    double ey = py - proj_y;
    return sqrt(ex * ex + ey * ey);
}

float normalize_heading(float deg) {
    float result = fmodf(deg, 360.0f);
    if (result < 0.0f) {
        result += 360.0f;
    }
    return result;
}

float heading_diff(float a, float b) {
    float d = normalize_heading(a) - normalize_heading(b);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

void lap_timer_init(QueueHandle_t gps_q,
                    QueueHandle_t vbo_q,
                    QueueHandle_t lap_event_q,
                    SemaphoreHandle_t session_mtx,
                    const TrackDefinition* track) {
    using namespace lap_timer_internal;

    s_gps_queue = gps_q;
    s_vbo_queue = vbo_q;
    s_lap_event_queue = lap_event_q;
    s_session_mutex = session_mtx;

    // Seed the shadow from the track pointer the caller supplied (which
    // is typically &active_track at the point init runs — boot is
    // single-threaded so no mutex needed).  s_track always points at the
    // shadow so hot-path readers are decoupled from active_track's
    // multi-writer state.
    if (track != nullptr) {
        s_track_shadow = *track;
    }
    s_track = &s_track_shadow;

    s_history_count = 0;
    s_current_sector = 0;
    s_lap_start_us = 0;
    s_sector_start_us = 0;
    s_best_lap_time_ms = -1;
    s_first_crossing = true;
    s_lap_point_count = 0;

    if (s_lap_points == nullptr) {
        s_lap_points = (GpsPoint*)ps_malloc(sizeof(GpsPoint) * MAX_LAP_POINTS);
        if (s_lap_points == nullptr) {
            Serial.println("[lap_timer] WARN: PSRAM alloc failed — delta reference disabled");
        }
    }

    for (int i = 0; i < MAX_SECTORS; i++) {
        s_arm_distance[i] = 0.0;
        s_arm_ready[i] = true;
        s_debounce_active[i] = false;
        s_debounce_remaining[i] = 0;
        s_debounce_crossing_us[i] = 0;
        s_crossing_candidate_active[i] = false;
    }
}

void lap_timer_reset(void) {
    using namespace lap_timer_internal;

    s_history_count = 0;
    s_current_sector = 0;
    s_lap_start_us = 0;
    s_sector_start_us = 0;
    s_best_lap_time_ms = -1;
    s_first_crossing = true;
    s_lap_point_count = 0;

    for (int i = 0; i < MAX_SECTORS; i++) {
        s_arm_distance[i] = 0.0;
        s_arm_ready[i] = true;
        s_debounce_active[i] = false;
        s_debounce_remaining[i] = 0;
        s_debounce_crossing_us[i] = 0;
        s_crossing_candidate_active[i] = false;
    }

    delta_free_reference();
    delta_init();
}

void lap_timer_set_track(const TrackDefinition* track) {
    if (!track) {
        return;
    }

    using namespace lap_timer_internal;
    extern TrackDefinition active_track;

    // Writer side of active_track is protected by session_mutex and
    // signalled to lap_timer_task via a version bump.  lap_timer_task
    // re-reads active_track into its shadow on its next iteration.
    if (s_session_mutex != nullptr) {
        xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    }
    active_track = *track;
    lap_timer_bump_active_track_version_locked();
    if (s_session_mutex != nullptr) {
        xSemaphoreGive(s_session_mutex);
    }

    lap_timer_reset();
    Serial.printf("[lap_timer] Track changed to: %s\n", track->name);

    // Re-emit the [track] summary on every runtime track change so
    // tooling that parses the serial boot log (tools/live_map.py) can
    // update its drawn P1/P2 geometry.  Without this, live_map keeps
    // showing the track that was active at firmware boot even after
    // the user creates/selects a new one.  Matches the format used
    // by the boot-time emit in main.cpp so live_map's existing
    // _P1_RE / _P2_RE / _HEAD_RE regexes pick it up unmodified.
    double lat1 = track->start_finish.lat1_deg;
    double lon1 = track->start_finish.lon1_deg;
    double lat2 = track->start_finish.lat2_deg;
    double lon2 = track->start_finish.lon2_deg;
    Serial.printf("[track] START/FINISH line (runtime change):\n");
    Serial.printf("[track]   p1 = (%.7f, %.7f)\n", lat1, lon1);
    Serial.printf("[track]   p2 = (%.7f, %.7f)\n", lat2, lon2);
    Serial.printf("[track]   valid_heading = %.1f deg\n",
                  track->start_finish.valid_heading_deg);
}

void lap_timer_active_track_changed_locked() {
    lap_timer_internal::lap_timer_bump_active_track_version_locked();
}
