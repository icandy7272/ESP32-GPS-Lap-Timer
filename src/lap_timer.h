#pragma once

// ============================================================
// Lap Timer Module — ESP32-S3 GPS Lap Timer
//
// Consumes GpsPoint from gps_queue, performs:
//   - Start/finish and sector line crossing detection
//   - Catmull-Rom spline interpolation for sub-sample timing
//   - Delta calculation vs reference lap
//   - VboEntry forwarding to storage task
//
// FreeRTOS task: Core 0, priority 20, stack 8192
// See docs/ARCHITECTURE.md section 2 for data flow.
// ============================================================

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "types.h"

// --- Public API -----------------------------------------------

/// Initialise lap timer module with required FreeRTOS handles.
/// Must be called before creating the lap_timer_task.
///   gps_q        - input queue of GpsPoint (depth 4)
///   vbo_q        - output queue of VboEntry (depth 256)
///   lap_event_q  - output queue of LapEvent (depth 16)
///   session_mtx  - mutex protecting SessionState
///   track        - pointer to loaded TrackDefinition (lifetime >= session)
void lap_timer_init(QueueHandle_t    gps_q,
                    QueueHandle_t    vbo_q,
                    QueueHandle_t    lap_event_q,
                    SemaphoreHandle_t session_mtx,
                    const TrackDefinition* track);

/// FreeRTOS task entry point.
/// Create with: xTaskCreatePinnedToCore(lap_timer_task, "lap_tmr",
///              8192, NULL, 20, NULL, 0);
void lap_timer_task(void* param);

/// Reset all lap timer state for a new session.
/// Clears history, arming, debounce, lap points, best lap.
/// Does NOT stop the task or touch queues.
void lap_timer_reset(void);

/// Change the active track at runtime.
/// Copies the track definition and resets the lap timer state.
/// Thread-safe: called from wifi task on Core 1.
void lap_timer_set_track(const TrackDefinition* track);

/// Signal that `active_track` has been mutated in place.
/// Caller MUST be holding session_mutex (the mutex that was passed to
/// lap_timer_init) around the mutation and this call.  lap_timer_task
/// picks up the change on its next iteration via a private shadow copy.
/// Use this when an API handler writes active_track directly (e.g. the
/// memset on delete).
void lap_timer_active_track_changed_locked();

// --- Math Helpers (exposed for unit testing) ------------------

/// Haversine distance in metres between two WGS84 points.
double haversine_m(double lat1, double lon1, double lat2, double lon2);

/// 2D cross product: (ax,ay) x (bx,by).
double cross_product_2d(double ax, double ay, double bx, double by);

/// Perpendicular distance from point (px,py) to segment (ax,ay)-(bx,by).
/// Also writes the projection parameter t (0..1) to *out_t if non-null.
double point_to_segment_distance(double px, double py,
                                 double ax, double ay,
                                 double bx, double by,
                                 double* out_t);

/// Normalize heading to [0, 360).
float normalize_heading(float deg);

/// Signed heading difference (a - b), result in [-180, +180].
float heading_diff(float a, float b);
