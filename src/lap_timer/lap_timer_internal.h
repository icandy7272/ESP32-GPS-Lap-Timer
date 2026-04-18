#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "../lap_timer.h"

namespace lap_timer_internal {

// === Crossing gate thresholds ===
// Two sets of values, selected at compile time by WALKING_TEST_MODE:
//   - PRODUCTION (default, WALKING_TEST_MODE undefined):
//       Real-track values. Rejects spurious crossings and short laps.
//   - WALKING_TEST_MODE (env: esp32-s3-devkitc-1-walking-test):
//       Relaxed values so walking a small loop around the start/finish
//       line actually produces valid laps and a best-lap reference.
//       Opt-in only — default `pio run` never sets this.
//
// LAP_SHORT_THRESHOLD_MS in src/session.cpp is gated off the SAME macro
// so lap_timer and session can't drift apart.
//
// See docs/TEST_MODES.md for the full parameter matrix and deployment
// checklist.
//
// MIN_CROSSING_SPEED_KMH is kept on BOTH paths: it rejects stationary GPS
// drift (speed=0) without affecting any real walking/driving case.
#ifdef WALKING_TEST_MODE
// 2026-04-18 second pass: tighten HEADING_WINDOW to match production's
// 60°.  The earlier 180° "disabled" value caused every walking loop
// that enclosed the start/finish segment to count TWICE per lap (once
// entering, once leaving).  u-blox M9N heading at walking pace is
// actually accurate enough (~±15°) to gate on, so 60° cleanly rejects
// the return-direction crossing without rejecting real laps.
static constexpr float HEADING_WINDOW = 60.0f;
// Bumped 3m → 6m on the same pass.  3m is ~3 walking steps and is
// easily covered inside a tight turn near the line without actually
// completing a lap; 6m forces the walker to have progressed a full
// real step-away from the line before a new crossing can arm.
static constexpr double ARM_DISTANCE_M = 6.0;
// Keep the 8s floor; see 2026-04-18 walk analysis.  A ~10m loop at
// brisk walk pace comes out to ≈ 9s, which is still accepted.
static constexpr int32_t MIN_LAP_TIME_MS = 8000;
#else
static constexpr float HEADING_WINDOW = 60.0f;
static constexpr double ARM_DISTANCE_M = 10.0;
static constexpr int32_t MIN_LAP_TIME_MS = 15000;
#endif
static constexpr float MIN_CROSSING_SPEED_KMH = 1.0f;
static constexpr int DEBOUNCE_SAMPLES = 2;
static constexpr float MAX_LAP_RATIO = 1.5f;
static constexpr int SPLINE_HISTORY = 4;
static constexpr int BINARY_SEARCH_ITS = 12;
static constexpr int MAX_LAP_POINTS = 4096;

extern QueueHandle_t s_gps_queue;
extern QueueHandle_t s_vbo_queue;
extern QueueHandle_t s_lap_event_queue;
extern SemaphoreHandle_t s_session_mutex;

// --- Active track shadowing ---
//
// `active_track` (in main.cpp) is the canonical track definition.  It is
// read by multiple tasks but only safe to mutate while holding
// s_session_mutex.  lap_timer_task owns s_track_shadow — a private copy
// refreshed from active_track under the mutex whenever
// s_active_track_version changes.  Hot-path readers (process_line,
// update_session_delta) dereference s_track which points at the shadow,
// so they never race with WiFi-side writers doing memset/assignment.
extern TrackDefinition s_track_shadow;
extern volatile uint32_t s_active_track_version;
extern const TrackDefinition* s_track;

// Bump the active-track version counter.  Must be called with
// s_session_mutex already held and immediately after writing active_track.
// lap_timer_task picks up the change on its next iteration.
void lap_timer_bump_active_track_version_locked();

extern GpsPoint s_history[SPLINE_HISTORY];
extern int s_history_count;

extern double s_arm_distance[MAX_SECTORS];
extern bool s_arm_ready[MAX_SECTORS];

extern int s_debounce_remaining[MAX_SECTORS];
extern bool s_debounce_active[MAX_SECTORS];
extern int64_t s_debounce_crossing_us[MAX_SECTORS];
extern double s_debounce_expected_sign[MAX_SECTORS];

extern int s_current_sector;
extern int64_t s_lap_start_us;
extern int64_t s_sector_start_us;
extern int32_t s_best_lap_time_ms;
extern bool s_first_crossing;

extern GpsPoint* s_lap_points;
extern int s_lap_point_count;

void history_push(const GpsPoint* pt);
const GpsPoint* history_get(int index);

void process_line(int line_idx,
                  const DetectionLine* line,
                  const GpsPoint* prev,
                  const GpsPoint* curr);

void handle_finish_crossing(int64_t crossing_us);
void handle_sector_crossing(int line_idx, int64_t crossing_us);

void emit_lap_event(uint8_t event_type, int sector_index, int64_t crossing_us);
bool is_lap_valid(int32_t lap_time_ms);
void update_session_delta(const GpsPoint* curr);

}  // namespace lap_timer_internal
