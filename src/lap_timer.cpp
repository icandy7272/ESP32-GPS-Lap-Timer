// ============================================================
// Lap Timer Implementation — ESP32-S3 GPS Lap Timer
//
// Line crossing detection with Catmull-Rom spline interpolation,
// sector/lap state management, and VBO entry forwarding.
//
// Runs on Core 0 at 25Hz (40ms per GPS fix).
// Zero dynamic allocation in the hot path.
// ============================================================

#include "lap_timer.h"
#include "delta.h"

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <math.h>
#include <stdlib.h>  // ps_malloc

// --- Constants ------------------------------------------------

static const double  DEG_TO_RAD_D      = M_PI / 180.0;
static const double  EARTH_RADIUS_M    = 6371000.0;
static const float   HEADING_WINDOW    = 60.0f;   // +/- degrees for valid crossing
static const double  ARM_DISTANCE_M    = 50.0;    // travel before re-arming
static const int     DEBOUNCE_SAMPLES  = 2;        // consecutive confirms needed
static const int32_t MIN_LAP_TIME_MS   = 15000;    // 15s minimum valid lap
static const float   MAX_LAP_RATIO     = 1.5f;     // max ratio vs best lap

// Catmull-Rom spline needs 4 points; binary search iterations
static const int     SPLINE_HISTORY    = 4;
static const int     BINARY_SEARCH_ITS = 12;

// --- Module State (file-scoped, no globals leaked) ------------

static QueueHandle_t     s_gps_queue;
static QueueHandle_t     s_vbo_queue;
static QueueHandle_t     s_lap_event_queue;
static SemaphoreHandle_t s_session_mutex;
static const TrackDefinition* s_track;

// Point history ring buffer for spline interpolation
static GpsPoint s_history[SPLINE_HISTORY];
static int      s_history_count = 0;

// Per-line arming state (start/finish + up to 3 sector lines)
static double s_arm_distance[MAX_SECTORS];   // distance since last crossing
static bool   s_arm_ready[MAX_SECTORS];      // true = can detect crossing

// Debounce state per line
static int  s_debounce_remaining[MAX_SECTORS]; // samples remaining to confirm
static bool s_debounce_active[MAX_SECTORS];    // debounce in progress
static int64_t s_debounce_crossing_us[MAX_SECTORS]; // tentative crossing time

// Lap/sector state
static int     s_current_sector    = 0;
static int64_t s_lap_start_us      = 0;
static int64_t s_sector_start_us   = 0;
static int32_t s_best_lap_time_ms  = -1;
static bool    s_first_crossing    = true;  // out-lap until first finish line

// Current lap GPS point buffer (for delta reference)
// Stored in PSRAM — up to 4096 points (~164s at 25Hz)
static constexpr int MAX_LAP_POINTS = 4096;
static GpsPoint* s_lap_points = nullptr;
static int       s_lap_point_count = 0;

// --- Math Helpers ---------------------------------------------

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
    if (d > 180.0f)  d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

// --- Forward Declarations -------------------------------------

static void handle_finish_crossing(int64_t crossing_us);
static void handle_sector_crossing(int line_idx, int64_t crossing_us);

// --- History Management ---------------------------------------

static void history_push(const GpsPoint* pt) {
    if (s_history_count < SPLINE_HISTORY) {
        s_history[s_history_count] = *pt;
        s_history_count++;
    } else {
        // Shift left, append new point
        for (int i = 0; i < SPLINE_HISTORY - 1; i++) {
            s_history[i] = s_history[i + 1];
        }
        s_history[SPLINE_HISTORY - 1] = *pt;
    }
}

static const GpsPoint* history_get(int index) {
    // index 0 = oldest in buffer
    return &s_history[index];
}

// --- Catmull-Rom Spline Interpolation -------------------------

/// Evaluate Catmull-Rom at parameter u in [0,1] for one axis.
/// p0..p3 are the four control values.
static double catmull_rom_eval(double p0, double p1,
                               double p2, double p3, double u) {
    double u2 = u * u;
    double u3 = u2 * u;
    return 0.5 * ((2.0 * p1)
                 + (-p0 + p2) * u
                 + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * u2
                 + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * u3);
}

/// Evaluate spline position (lat, lon) at parameter u.
static void spline_position(const GpsPoint* p0, const GpsPoint* p1,
                            const GpsPoint* p2, const GpsPoint* p3,
                            double u, double* out_lat, double* out_lon) {
    *out_lat = catmull_rom_eval(p0->lat_deg, p1->lat_deg,
                                p2->lat_deg, p3->lat_deg, u);
    *out_lon = catmull_rom_eval(p0->lon_deg, p1->lon_deg,
                                p2->lon_deg, p3->lon_deg, u);
}

/// Check if a point (plat, plon) is on the same side of the detection line
/// as determined by the sign. Returns the cross product sign.
static double side_of_line(double plat, double plon,
                           const DetectionLine* line) {
    double lx = line->lat2_deg - line->lat1_deg;
    double ly = line->lon2_deg - line->lon1_deg;
    double px = plat - line->lat1_deg;
    double py = plon - line->lon1_deg;
    return cross_product_2d(lx, ly, px, py);
}

/// Binary search along Catmull-Rom spline for the zero-crossing point.
/// Returns parameter t in [0,1] relative to segment p2-p3 interval.
static double spline_find_crossing(const GpsPoint* p0, const GpsPoint* p1,
                                   const GpsPoint* p2, const GpsPoint* p3,
                                   const DetectionLine* line) {
    double lo = 0.0;
    double hi = 1.0;
    double sign_lo;
    {
        double lat, lon;
        spline_position(p0, p1, p2, p3, lo, &lat, &lon);
        sign_lo = side_of_line(lat, lon, line);
    }

    for (int i = 0; i < BINARY_SEARCH_ITS; i++) {
        double mid = (lo + hi) * 0.5;
        double lat, lon;
        spline_position(p0, p1, p2, p3, mid, &lat, &lon);
        double sign_mid = side_of_line(lat, lon, line);

        if ((sign_lo > 0.0) == (sign_mid > 0.0)) {
            lo = mid;
            sign_lo = sign_mid;
        } else {
            hi = mid;
        }
    }
    return (lo + hi) * 0.5;
}

/// Linear interpolation fallback when < 4 history points.
static double linear_crossing_t(const GpsPoint* prev, const GpsPoint* curr,
                                const DetectionLine* line) {
    double s_prev = side_of_line(prev->lat_deg, prev->lon_deg, line);
    double s_curr = side_of_line(curr->lat_deg, curr->lon_deg, line);
    double denom = s_curr - s_prev;
    if (fabs(denom) < 1e-15) {
        return 0.5;
    }
    double t = -s_prev / denom;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return t;
}

// --- Crossing Detection ---------------------------------------

/// Compute precise crossing time using spline or linear interpolation.
static int64_t compute_crossing_time(const DetectionLine* line) {
    if (s_history_count >= SPLINE_HISTORY) {
        // Crossing is between history[2] (prev) and history[3] (curr).
        // Catmull-Rom with (p0,p1,p2,p3) interpolates p1→p2.
        // To interpolate h[2]→h[3], pass (h[1], h[2], h[3], h[3]) —
        // duplicating the last point as the forward tangent control.
        const GpsPoint* p0 = history_get(1);
        const GpsPoint* p1 = history_get(2);
        const GpsPoint* p2 = history_get(3);
        const GpsPoint* p3 = history_get(3);  // extrapolate: repeat last
        double t = spline_find_crossing(p0, p1, p2, p3, line);
        int64_t dt = p2->timestamp_us - p1->timestamp_us;
        return p1->timestamp_us + (int64_t)(t * (double)dt);
    }

    // Fallback: linear interpolation between last two points
    const GpsPoint* prev = history_get(s_history_count - 2);
    const GpsPoint* curr = history_get(s_history_count - 1);
    double t = linear_crossing_t(prev, curr, line);
    int64_t dt = curr->timestamp_us - prev->timestamp_us;
    return prev->timestamp_us + (int64_t)(t * (double)dt);
}

/// Check if the GPS track crosses a detection line between prev and curr.
/// Returns true if crossing detected (before debounce validation).
static bool has_crossed_line(const GpsPoint* prev, const GpsPoint* curr,
                             const DetectionLine* line) {
    // Signed cross products: check if prev and curr are on opposite sides
    double s_prev = side_of_line(prev->lat_deg, prev->lon_deg, line);
    double s_curr = side_of_line(curr->lat_deg, curr->lon_deg, line);

    // Same side => no crossing
    if ((s_prev > 0.0) == (s_curr > 0.0)) {
        return false;
    }

    // Heading validation: GPS heading must be within +/- HEADING_WINDOW
    float hdiff = heading_diff(curr->heading_deg, line->valid_heading_deg);
    if (fabsf(hdiff) > HEADING_WINDOW) {
        return false;
    }

    return true;
}

/// Update arming distance for a specific line index.
static void update_arm_distance(int line_idx,
                                const GpsPoint* prev,
                                const GpsPoint* curr) {
    double dist = haversine_m(prev->lat_deg, prev->lon_deg,
                              curr->lat_deg, curr->lon_deg);
    s_arm_distance[line_idx] += dist;
    if (s_arm_distance[line_idx] > ARM_DISTANCE_M) {
        s_arm_ready[line_idx] = true;
    }
}

/// Reset arming state after a confirmed crossing.
static void reset_arm(int line_idx) {
    s_arm_distance[line_idx] = 0.0;
    s_arm_ready[line_idx] = false;
}

// --- Debounce Validation --------------------------------------

/// Start debounce: record tentative crossing time and the expected side.
static double s_debounce_expected_sign[MAX_SECTORS];

static void debounce_start(int line_idx, int64_t crossing_us,
                           double crossed_side_sign) {
    s_debounce_active[line_idx]        = true;
    s_debounce_remaining[line_idx]     = DEBOUNCE_SAMPLES;
    s_debounce_crossing_us[line_idx]   = crossing_us;
    s_debounce_expected_sign[line_idx] = crossed_side_sign;
}

/// Feed a new sample into debounce. Returns true when confirmed.
/// Checks that the point is still on the crossed side of the line.
/// If it bounced back, cancels the tentative crossing.
static bool debounce_feed(int line_idx,
                          const GpsPoint* curr,
                          const DetectionLine* line) {
    if (!s_debounce_active[line_idx]) {
        return false;
    }

    // Check current point is still on the expected side
    double current_sign = side_of_line(curr->lat_deg, curr->lon_deg, line);
    bool same_side = (current_sign * s_debounce_expected_sign[line_idx]) > 0;

    if (!same_side) {
        // Bounced back — false crossing, cancel
        s_debounce_active[line_idx] = false;
        return false;
    }

    s_debounce_remaining[line_idx]--;
    if (s_debounce_remaining[line_idx] <= 0) {
        s_debounce_active[line_idx] = false;
        return true;  // confirmed
    }
    return false;
}

/// Cancel a pending debounce (crossing rejected).
static void __attribute__((unused)) debounce_cancel(int line_idx) {
    s_debounce_active[line_idx] = false;
    s_debounce_remaining[line_idx] = 0;
}

// --- Lap Event Emission ---------------------------------------

static void emit_lap_event(uint8_t event_type, int sector_index,
                           int64_t crossing_us) {
    LapEvent event;
    event.event_type   = event_type;
    event.sector_index = sector_index;
    event.crossing_us  = crossing_us;
    xQueueSend(s_lap_event_queue, &event, 0);
}

// --- Lap Validity Check ---------------------------------------

static bool is_lap_valid(int32_t lap_time_ms) {
    if (lap_time_ms < MIN_LAP_TIME_MS) {
        return false;  // LAP_STATUS_SHORT
    }
    if (s_best_lap_time_ms > 0
        && lap_time_ms > (int32_t)(s_best_lap_time_ms * MAX_LAP_RATIO)) {
        return false;  // LAP_STATUS_SLOW
    }
    return true;
}

// --- VBO Entry Forwarding -------------------------------------

static void forward_vbo_entry(const GpsPoint* pt) {
    VboEntry entry;
    entry.satellites  = pt->satellites;
    entry.timestamp_us = pt->timestamp_us;
    entry.lat_deg     = pt->lat_deg;
    entry.lon_deg     = pt->lon_deg;
    entry.speed_kmh   = pt->speed_kmh;
    entry.heading_deg = pt->heading_deg;
    entry.height_m    = pt->height_m;
    xQueueSend(s_vbo_queue, &entry, 0);
}

// --- Line Processing ------------------------------------------

/// Process crossing detection for one detection line.
/// line_idx: 0 = start/finish, 1..3 = sector splits.
static void process_line(int line_idx, const DetectionLine* line,
                         const GpsPoint* prev, const GpsPoint* curr) {
    // Update arming distance
    update_arm_distance(line_idx, prev, curr);

    // If debounce is active, feed it
    if (s_debounce_active[line_idx]) {
        if (debounce_feed(line_idx, curr, line)) {
            // Debounce confirmed: emit the event with pre-computed time
            int64_t crossing_us = s_debounce_crossing_us[line_idx];
            reset_arm(line_idx);

            if (line_idx == 0) {
                handle_finish_crossing(crossing_us);
            } else {
                handle_sector_crossing(line_idx, crossing_us);
            }
        }
        return;
    }

    // Check for new crossing
    if (!s_arm_ready[line_idx]) {
        return;
    }
    if (!has_crossed_line(prev, curr, line)) {
        return;
    }

    // Crossing detected: compute precise time immediately
    int64_t crossing_us = compute_crossing_time(line);

    // Record which side the current point is on (the "crossed-to" side)
    double crossed_side = side_of_line(curr->lat_deg, curr->lon_deg, line);

    // Start debounce validation — must stay on this side
    debounce_start(line_idx, crossing_us, crossed_side);
}

// --- Crossing Handlers ----------------------------------------

/// Handle confirmed finish-line crossing.
static void handle_finish_crossing(int64_t crossing_us) {
    if (s_first_crossing) {
        // First crossing: end of out-lap, start timing
        s_first_crossing = false;
        s_lap_start_us   = crossing_us;
        s_sector_start_us = crossing_us;
        s_current_sector  = 0;
        delta_set_lap_start(crossing_us);
        delta_reset_elapsed();
        emit_lap_event(LAP_EVENT_FINISH, 0, crossing_us);
        return;
    }

    // Lap complete
    int64_t elapsed_us = crossing_us - s_lap_start_us;
    int32_t lap_time_ms = (int32_t)(elapsed_us / 1000);

    bool is_new_best = false;
    if (is_lap_valid(lap_time_ms)) {
        if (s_best_lap_time_ms < 0 || lap_time_ms < s_best_lap_time_ms) {
            s_best_lap_time_ms = lap_time_ms;
            is_new_best = true;
        }
    }

    // Update delta reference if this is the new best lap
    if (is_new_best && s_lap_points != nullptr && s_lap_point_count > 0) {
        delta_set_reference(s_lap_points, s_lap_point_count);
    }

    emit_lap_event(LAP_EVENT_FINISH, 0, crossing_us);

    // Reset lap point buffer for next lap
    s_lap_point_count = 0;

    // Reset for next lap
    s_lap_start_us    = crossing_us;
    s_sector_start_us = crossing_us;
    s_current_sector  = 0;
    delta_set_lap_start(crossing_us);
    delta_reset_elapsed();
}

/// Handle confirmed sector-line crossing.
static void handle_sector_crossing(int line_idx, int64_t crossing_us) {
    // Only accept sector crossings in order
    int expected_sector = s_current_sector + 1;
    if (line_idx != expected_sector) {
        return;  // out of order, ignore
    }

    emit_lap_event(LAP_EVENT_SECTOR, line_idx, crossing_us);

    s_sector_start_us = crossing_us;
    s_current_sector  = line_idx;
}

// --- Delta Update ---------------------------------------------

static void update_session_delta(const GpsPoint* curr) {
    int32_t delta_ms = delta_calculate(curr);
    bool valid = delta_is_valid();

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        // Write delta into shared SessionState
        // The session task owns the full SessionState; we only touch delta fields
        extern SessionState session_state;
        session_state.delta_ms      = delta_ms;
        session_state.delta_valid   = valid;
        session_state.off_track     = delta_is_off_track();
        session_state.gps_fix_ok    = curr->fix_3d;
        session_state.gps_satellites = curr->satellites;
        xSemaphoreGive(s_session_mutex);
    }
    // If mutex times out (2ms), skip this update. Non-critical.
}

// --- Public API -----------------------------------------------

void lap_timer_init(QueueHandle_t    gps_q,
                    QueueHandle_t    vbo_q,
                    QueueHandle_t    lap_event_q,
                    SemaphoreHandle_t session_mtx,
                    const TrackDefinition* track) {
    s_gps_queue       = gps_q;
    s_vbo_queue       = vbo_q;
    s_lap_event_queue = lap_event_q;
    s_session_mutex   = session_mtx;
    s_track           = track;

    s_history_count    = 0;
    s_current_sector   = 0;
    s_lap_start_us     = 0;
    s_sector_start_us  = 0;
    s_best_lap_time_ms = -1;
    s_first_crossing   = true;
    s_lap_point_count  = 0;

    // Allocate lap point buffer in PSRAM
    if (s_lap_points == nullptr) {
        s_lap_points = (GpsPoint*)ps_malloc(sizeof(GpsPoint) * MAX_LAP_POINTS);
    }

    // Initialise arming: start/finish (index 0) + sector lines
    for (int i = 0; i < MAX_SECTORS; i++) {
        s_arm_distance[i]        = 0.0;
        s_arm_ready[i]           = true;  // armed at startup
        s_debounce_active[i]     = false;
        s_debounce_remaining[i]  = 0;
        s_debounce_crossing_us[i] = 0;
    }
}

void lap_timer_reset(void) {
    s_history_count    = 0;
    s_current_sector   = 0;
    s_lap_start_us     = 0;
    s_sector_start_us  = 0;
    s_best_lap_time_ms = -1;
    s_first_crossing   = true;
    s_lap_point_count  = 0;

    for (int i = 0; i < MAX_SECTORS; i++) {
        s_arm_distance[i]        = 0.0;
        s_arm_ready[i]           = true;
        s_debounce_active[i]     = false;
        s_debounce_remaining[i]  = 0;
        s_debounce_crossing_us[i] = 0;
    }

    // Clear delta reference for fresh session
    delta_free_reference();
    delta_init();
}

void lap_timer_task(void* param) {
    (void)param;

    // Register with task watchdog (5s timeout)
    esp_task_wdt_add(NULL);

    GpsPoint curr;
    GpsPoint prev;
    bool has_prev = false;

    for (;;) {
        // Block until GPS data arrives (max wait = 200ms for WDT safety)
        if (xQueueReceive(s_gps_queue, &curr, pdMS_TO_TICKS(200)) != pdTRUE) {
            esp_task_wdt_reset();
            continue;
        }

        // Push into history for spline interpolation
        history_push(&curr);

        if (has_prev) {
            // --- Line crossing detection ---
            // Check start/finish line
            process_line(0, &s_track->start_finish, &prev, &curr);

            // Check sector lines
            for (int i = 0; i < s_track->sector_count - 1; i++) {
                process_line(i + 1, &s_track->sectors[i], &prev, &curr);
            }

            // --- Delta calculation ---
            update_session_delta(&curr);
        }

        // --- Buffer point for delta reference ---
        if (s_lap_points != nullptr && s_lap_point_count < MAX_LAP_POINTS
            && !s_first_crossing) {
            s_lap_points[s_lap_point_count++] = curr;
        }

        // --- Forward to VBO writer ---
        forward_vbo_entry(&curr);

        // Advance
        prev = curr;
        has_prev = true;

        // Feed watchdog
        esp_task_wdt_reset();
    }
}
