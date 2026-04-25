#pragma once

// ============================================================
// Lap Timer Module — ESP32-S3 GPS Lap Timer
//
// Consumes GpsFixBundle from gps_queue, performs:
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
///   gps_q        - input queue of GpsFixBundle (depth 4)
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

// --- Draft Validation Line (Phase A) ------------------------------
//
// During track creation the operator wants to "walk across the line a
// couple of times and verify crossings fire before committing to save".
// The lap_timer runs the SAME crossing geometry against this line as
// it does against the active track's start/finish, BUT does not arm /
// debounce / record laps — it only emits structured candidate events
// over serial (prefix [xing-draft]) and populates a ring-buffer the
// web UI can poll.  See docs/superpowers/plans/2026-04-18-finish-line-
// live-map-debugging.md "Validation Mode".
//
// Thread-safety: the setter / clearer grab an internal mutex.  The
// lap_timer task reads a shadow copy at the top of each iteration.
struct DraftCandidateEvent {
    int64_t timestamp_us;
    double  u;
    double  overshoot_m;
    float   hdiff_deg;
    bool    accepted;     // PASS vs REJECT
    uint8_t reason;       // DraftCandidateReason
};

enum DraftCandidateReason : uint8_t {
    DRAFT_REASON_NONE = 0,                     // PASS, on segment
    DRAFT_REASON_SEGMENT = 1,                  // PASS alias
    DRAFT_REASON_EXTENSION = 2,                // PASS, within tolerance
    DRAFT_REASON_HEADING_MISMATCH = 3,         // REJECT
    DRAFT_REASON_OUTSIDE_ENDPOINT_TOLERANCE = 4 // REJECT
};

// Which client surface installed the current draft line.  Used by the
// setter to log cross-surface takeovers (serial clobbering web or vice
// versa) and reported in the atomic snapshot so clients can tell when
// they've been displaced.
enum DraftValidationOwner : uint8_t {
    DRAFT_OWNER_NONE   = 0,
    DRAFT_OWNER_SERIAL = 1,   // serial `mark p2` auto-install (main.cpp)
    DRAFT_OWNER_WEB    = 2,   // POST /api/tracks/draft_validation
};

// Atomic snapshot of the whole draft-validation state.  Populated by
// lap_timer_get_draft_validation_snapshot() under a single critical
// section so clients can trust that every field belongs to the same
// session — no more "active=false but events non-empty" races caused
// by reading each field with its own lock.  Also exposes the
// session_id (monotonically bumped on every set/clear) that clients
// compare against their cached value to drop stale responses.
struct DraftValidationSnapshot {
    bool     active;
    uint32_t session_id;
    uint32_t accepted;
    uint32_t rejected;
    uint8_t  owner;            // DraftValidationOwner
    int      event_count;      // How many entries written to `events`
};

/// Install a draft detection line.  The lap_timer evaluates crossing
/// geometry against this line without touching lap / session state.
/// Safe to call from any task; grabs a private mutex.  Pass nullptr
/// for `line` to clear.  Also resets the accepted/rejected counters
/// and candidate ring buffer.  Bumps the session_id so any in-flight
/// candidate evaluation or client response tagged with the old id is
/// discarded by the firmware/client filters.
///
/// Returns the new session_id assigned to this call (or 0 for a
/// clear, which the atomic snapshot will also report as active=false).
uint32_t lap_timer_set_draft_validation_line(const DetectionLine* line,
                                             DraftValidationOwner owner);

/// Single-lock snapshot: reads active/session_id/counts/owner/events
/// in one critical section.  `out_events` receives up to `max_events`
/// newest-first candidates; `snapshot.event_count` is the actual
/// number written.  Pass nullptr + 0 to skip events and read counters
/// only.
void lap_timer_get_draft_validation_snapshot(DraftValidationSnapshot* snapshot,
                                             DraftCandidateEvent* out_events,
                                             int max_events);

/// Save-gate check: returns true iff a draft line is installed AND
/// its P1/P2/heading match the posted `line` (within `tol_m` metres
/// for endpoints and `tol_deg` degrees for heading) AND the accepted
/// count is >= `min_accepted`.  Called from both the web POST
/// /api/tracks handler and the serial `track save` handler so the
/// save gate is enforced on the firmware, not the client.
bool lap_timer_draft_validation_passes_gate(const DetectionLine* line,
                                            double  tol_m,
                                            double  tol_deg,
                                            uint32_t min_accepted);

/// Minimum accepted crossings the save gate requires.  Selected at
/// compile time: 1 for walking-test, 2 for production, matching the
/// WEB_UI_MIN_ACCEPTED_CROSSINGS_LIT literal used by the JS UI.
uint32_t lap_timer_draft_validation_min_accepted();

/// Atomic "clear only if the current session_id is still `expected`".
/// Exists so the HTTP DELETE handler can scope client-driven cleanup
/// to a specific session the client had cached — closing the
/// GET-then-unscoped-DELETE TOCTOU where a newer session installed
/// between the client's GET and its DELETE would be silently killed.
/// Codex P2 from 2026-04-20 round-4.
///
/// Single critical section:
///   - If s_session_id == expected_session_id: clears state (exactly
///     like set(nullptr)) and returns the new session_id assigned to
///     the clear.
///   - Otherwise: leaves state untouched and returns 0.
///
/// A return of 0 means "nothing was cleared" — the caller's desired
/// end-state (no-owned-session) is already the case for a different
/// reason, so it should be treated as success.
uint32_t lap_timer_clear_draft_validation_if(uint32_t expected_session_id);

// --- Deprecated aliases (kept until all callers migrate) ----------
// These were the initial Phase A/B API; the atomic snapshot replaces
// them but the old signatures compile so existing code paths keep
// working while the migration lands.  New code should use the
// snapshot.
void lap_timer_get_draft_validation_counts(uint32_t* out_accepted,
                                           uint32_t* out_rejected);
int  lap_timer_get_draft_validation_candidates(DraftCandidateEvent* out,
                                               int max_out);
bool lap_timer_draft_validation_active();

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
