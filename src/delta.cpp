// ============================================================
// Delta Calculation Implementation — ESP32-S3 GPS Lap Timer
//
// Position-Based Delta via polyline progress projection.
// Reference lap stored in PSRAM with pre-computed cumulative
// distances for O(1) progress lookup via binary search.
//
// Called at 25Hz from lap_timer_task on Core 0.
// Must not block. No dynamic allocation in the hot path.
// ============================================================

#include "delta.h"
#include "lap_timer.h"  // for math helpers

#include <Arduino.h>
#include <math.h>
#include <stdlib.h>  // for ps_malloc / free

// --- Constants ------------------------------------------------

static const double EARTH_RADIUS_M     = 6371000.0;
static const double OFF_TRACK_ENTER_M  = 30.0;  // go off-track above this
static const double OFF_TRACK_EXIT_M   = 20.0;  // return on-track below this
static const float  HEADING_FILTER_DEG = 90.0f;  // skip segments with heading diff > this
static const int    MAX_REFERENCE_PTS  = 4096;    // max points per reference lap (~164s at 25Hz)

// --- Module State (file-scoped) -------------------------------

// Reference polyline (PSRAM-allocated)
static GpsPoint* s_ref_points     = nullptr;
static int       s_ref_count      = 0;

// Pre-computed cumulative distances along reference polyline
static double*   s_ref_cum_dist   = nullptr;  // length = s_ref_count
static double    s_ref_total_dist = 0.0;

// Pre-computed headings for each segment
static float*    s_ref_headings   = nullptr;  // length = s_ref_count - 1

// Elapsed time at each reference point (relative to ref lap start)
static int64_t*  s_ref_elapsed_us = nullptr;  // length = s_ref_count

// Current state
static int64_t   s_lap_start_us   = 0;
static bool      s_has_reference   = false;
static bool      s_off_track       = false;
static int32_t   s_frozen_delta_ms = 0;  // delta value when off-track detected

// Last matched segment index for search locality
static int       s_last_seg_idx    = 0;

// --- Internal: Pre-computation --------------------------------

/// Compute heading from point A to point B in degrees [0, 360).
static float compute_heading(double lat1, double lon1,
                             double lat2, double lon2) {
    double dlat = lat2 - lat1;
    double dlon = lon2 - lon1;
    float deg = (float)(atan2(dlon, dlat) * 180.0 / M_PI);
    return normalize_heading(deg);
}

/// Pre-compute cumulative distances, headings, and elapsed times.
static void precompute_reference(void) {
    if (s_ref_count < 2) return;

    s_ref_cum_dist[0] = 0.0;
    int64_t start_us = s_ref_points[0].timestamp_us;
    s_ref_elapsed_us[0] = 0;

    for (int i = 1; i < s_ref_count; i++) {
        double d = haversine_m(s_ref_points[i - 1].lat_deg,
                               s_ref_points[i - 1].lon_deg,
                               s_ref_points[i].lat_deg,
                               s_ref_points[i].lon_deg);
        s_ref_cum_dist[i] = s_ref_cum_dist[i - 1] + d;
        s_ref_elapsed_us[i] = s_ref_points[i].timestamp_us - start_us;
        if ((i & 0xFF) == 0) { taskYIELD(); }  // yield Core 0 every 256 pts
    }
    s_ref_total_dist = s_ref_cum_dist[s_ref_count - 1];

    for (int i = 0; i < s_ref_count - 1; i++) {
        s_ref_headings[i] = compute_heading(
            s_ref_points[i].lat_deg, s_ref_points[i].lon_deg,
            s_ref_points[i + 1].lat_deg, s_ref_points[i + 1].lon_deg);
        if ((i & 0xFF) == 0) { taskYIELD(); }  // yield Core 0 every 256 pts
    }
}

// --- Internal: Polyline Projection ----------------------------

/// Result of projecting a point onto the reference polyline.
typedef struct {
    double progress;       // 0.0 ~ 1.0 along polyline
    double lateral_dist_m; // perpendicular distance to nearest segment
    int    segment_idx;    // index of the matched segment
    bool   valid;          // false if heading filter rejected all segments
} ProjectionResult;

/// Project a point onto the reference polyline.
/// Uses heading filter to avoid wrong-segment matching at crossovers.
/// Searches near s_last_seg_idx first for temporal locality.
static ProjectionResult project_to_polyline(const GpsPoint* pt) {
    ProjectionResult result;
    result.progress       = 0.0;
    result.lateral_dist_m = 1e9;
    result.segment_idx    = 0;
    result.valid          = false;

    if (s_ref_count < 2) return result;

    double best_dist = 1e9;
    int    best_seg  = -1;
    double best_t    = 0.0;

    // Convert current position to flat coordinates relative to first ref point
    // (approximation valid for short distances on a track)
    double cos_lat = cos(s_ref_points[0].lat_deg * M_PI / 180.0);

    double px = (pt->lat_deg - s_ref_points[0].lat_deg);
    double py = (pt->lon_deg - s_ref_points[0].lon_deg) * cos_lat;

    int seg_count = s_ref_count - 1;

    // Search window: check nearby segments first, expand if needed
    // Start from last matched segment for temporal locality
    for (int offset = 0; offset < seg_count; offset++) {
        int i = (s_last_seg_idx + offset) % seg_count;

        // Heading filter: skip if heading differs by > 90 degrees
        float hdiff = heading_diff(pt->heading_deg, s_ref_headings[i]);
        if (fabsf(hdiff) > HEADING_FILTER_DEG) {
            continue;
        }

        double ax = s_ref_points[i].lat_deg - s_ref_points[0].lat_deg;
        double ay = (s_ref_points[i].lon_deg - s_ref_points[0].lon_deg) * cos_lat;
        double bx = s_ref_points[i + 1].lat_deg - s_ref_points[0].lat_deg;
        double by = (s_ref_points[i + 1].lon_deg - s_ref_points[0].lon_deg) * cos_lat;

        double t;
        double dist = point_to_segment_distance(px, py, ax, ay, bx, by, &t);

        if (dist < best_dist) {
            best_dist = dist;
            best_seg  = i;
            best_t    = t;
            result.valid = true;
        }
    }

    if (!result.valid) return result;

    // Convert flat-coordinate distance back to metres
    // The flat coords are in degrees; multiply by ~111km for lat
    double deg_to_m = EARTH_RADIUS_M * M_PI / 180.0;
    result.lateral_dist_m = best_dist * deg_to_m;

    // Calculate progress along polyline
    double dist_along = s_ref_cum_dist[best_seg]
                      + best_t * (s_ref_cum_dist[best_seg + 1]
                                  - s_ref_cum_dist[best_seg]);
    result.progress = (s_ref_total_dist > 0.0)
                    ? (dist_along / s_ref_total_dist)
                    : 0.0;

    // Clamp to [0, 1]
    if (result.progress < 0.0) result.progress = 0.0;
    if (result.progress > 1.0) result.progress = 1.0;

    result.segment_idx = best_seg;
    s_last_seg_idx = best_seg;

    return result;
}

// --- Internal: Reference Time Lookup --------------------------

/// Look up the reference lap elapsed time at a given progress (0~1).
/// Uses binary search on cumulative distances for O(log n) lookup.
static int64_t ref_time_at_progress(double progress) {
    if (s_ref_count < 2) return 0;

    double target_dist = progress * s_ref_total_dist;

    // Binary search for the segment containing target_dist
    int lo = 0;
    int hi = s_ref_count - 1;
    while (lo < hi - 1) {
        int mid = (lo + hi) / 2;
        if (s_ref_cum_dist[mid] <= target_dist) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    // Interpolate within the segment [lo, hi]
    double seg_len = s_ref_cum_dist[hi] - s_ref_cum_dist[lo];
    double t = 0.0;
    if (seg_len > 1e-6) {
        t = (target_dist - s_ref_cum_dist[lo]) / seg_len;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
    }

    int64_t dt = s_ref_elapsed_us[hi] - s_ref_elapsed_us[lo];
    return s_ref_elapsed_us[lo] + (int64_t)(t * (double)dt);
}

// --- Off-Track Detection (with hysteresis) --------------------

static bool check_off_track(double lateral_dist_m) {
    if (s_off_track) {
        // Currently off-track: return on-track when below exit threshold
        if (lateral_dist_m < OFF_TRACK_EXIT_M) {
            s_off_track = false;
        }
    } else {
        // Currently on-track: go off-track when above enter threshold
        if (lateral_dist_m > OFF_TRACK_ENTER_M) {
            s_off_track = true;
        }
    }
    return s_off_track;
}

// --- Public API -----------------------------------------------

void delta_init(void) {
    s_ref_points     = nullptr;
    s_ref_count      = 0;
    s_ref_cum_dist   = nullptr;
    s_ref_headings   = nullptr;
    s_ref_elapsed_us = nullptr;
    s_ref_total_dist = 0.0;
    s_has_reference  = false;
    s_off_track      = false;
    s_frozen_delta_ms = 0;
    s_lap_start_us   = 0;
    s_last_seg_idx   = 0;
}

void delta_free_reference(void) {
    if (s_ref_points != nullptr) {
        free(s_ref_points);
        s_ref_points = nullptr;
    }
    if (s_ref_cum_dist != nullptr) {
        free(s_ref_cum_dist);
        s_ref_cum_dist = nullptr;
    }
    if (s_ref_headings != nullptr) {
        free(s_ref_headings);
        s_ref_headings = nullptr;
    }
    if (s_ref_elapsed_us != nullptr) {
        free(s_ref_elapsed_us);
        s_ref_elapsed_us = nullptr;
    }
    s_ref_count      = 0;
    s_ref_total_dist = 0.0;
    s_has_reference  = false;
    s_last_seg_idx   = 0;
}

void delta_set_reference(const GpsPoint* points, int count) {
    // Free any existing reference
    delta_free_reference();

    if (points == nullptr || count < 2) return;

    // Clamp to maximum
    if (count > MAX_REFERENCE_PTS) {
        count = MAX_REFERENCE_PTS;
    }

    // Allocate in PSRAM
    s_ref_points = (GpsPoint*)ps_malloc(sizeof(GpsPoint) * count);
    s_ref_cum_dist = (double*)ps_malloc(sizeof(double) * count);
    s_ref_headings = (float*)ps_malloc(sizeof(float) * (count - 1));
    s_ref_elapsed_us = (int64_t*)ps_malloc(sizeof(int64_t) * count);

    if (s_ref_points == nullptr || s_ref_cum_dist == nullptr
        || s_ref_headings == nullptr || s_ref_elapsed_us == nullptr) {
        // Allocation failed: clean up and bail
        delta_free_reference();
        return;
    }

    // Copy reference data
    extern int crash_bc_core0;
    crash_bc_core0 = 11;  // in delta_set_reference: memcpy
    memcpy(s_ref_points, points, sizeof(GpsPoint) * count);
    s_ref_count = count;

    // Pre-compute distances, headings, elapsed times
    crash_bc_core0 = 14;  // in precompute_reference
    precompute_reference();

    s_has_reference = true;
    s_off_track     = false;
    s_last_seg_idx  = 0;
}

int32_t delta_calculate(const GpsPoint* current) {
    if (!s_has_reference || current == nullptr) {
        return 0;
    }

    // Project current position onto reference polyline
    ProjectionResult proj = project_to_polyline(current);

    if (!proj.valid) {
        return s_frozen_delta_ms;
    }

    // Off-track check with hysteresis
    if (check_off_track(proj.lateral_dist_m)) {
        // Off-track: freeze delta at last known value
        return s_frozen_delta_ms;
    }

    // Guard against start/finish ambiguity: at the very start of a lap,
    // the GPS position near the start/finish line can project to the
    // END of the reference polyline (which is at the same location),
    // producing a huge negative delta.  Reject high-progress matches
    // in the first few seconds.
    int64_t current_elapsed_us = current->timestamp_us - s_lap_start_us;
    if (current_elapsed_us < 5000000 && proj.progress > 0.75) {
        return 0;
    }

    // Look up reference time at the same progress
    int64_t ref_elapsed_us = ref_time_at_progress(proj.progress);

    // Delta = current - reference (positive = slower)
    int64_t delta_us = current_elapsed_us - ref_elapsed_us;
    int32_t delta_ms = (int32_t)(delta_us / 1000);

    // Store for freeze on off-track
    s_frozen_delta_ms = delta_ms;

    return delta_ms;
}

bool delta_is_valid(void) {
    return s_has_reference && !s_off_track;
}

bool delta_has_reference(void) {
    return s_has_reference;
}

bool delta_is_off_track(void) {
    return s_off_track;
}

void delta_reset_elapsed(void) {
    s_off_track       = false;
    s_frozen_delta_ms = 0;
    s_last_seg_idx    = 0;
}

void delta_set_lap_start(int64_t start_us) {
    s_lap_start_us = start_us;
}
