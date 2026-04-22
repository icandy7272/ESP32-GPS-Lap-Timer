#pragma once

// ============================================================
// Session state machine — ESP32-S3 GPS Lap Timer
// Manages Ready -> Recording -> Finished lifecycle.
// Owns session_state (the single shared state object) behind
// session_mutex.  See docs/ARCHITECTURE.md section 3.
// ============================================================

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

#include "types.h"

// --- Session phase (internal state machine) ---

typedef enum {
    SESSION_READY     = 0,  // waiting for user to press record
    SESSION_RECORDING = 1,  // GPS data logged, crossing active
    SESSION_FINISHED  = 2,  // recording stopped, session saved
} SessionPhase;

// --- Shared state (read by display/wifi, written by session task) ---

extern SessionState       session_state;
extern SemaphoreHandle_t  session_mutex;

// --- Lifecycle ---

// Create mutex, zero session_state, store queue handles.
void session_init(QueueHandle_t lap_event_q, QueueHandle_t btn_session_q);

// FreeRTOS task entry — Core 1, priority 16, stack 4096.
// Blocks on lap_event_queue and btn_session_queue alternately.
void session_task(void* param);

// Transition Ready -> Recording.
// Sets is_recording, copies track_name, calls storage_start_session().
void session_start_recording(const char* track_name);

// Transition Recording -> Finished.
// Clears is_recording, calls storage_end_session().
// Returns true only when the final session file was committed.
bool session_stop_recording();

// --- Track switching (shared between serial console and HTTP API) ---
//
// Atomically verifies "not recording" + looks up / auto-detects the
// target track + updates session_state.track_name, all under a
// single session_mutex critical section.  Fails CLOSED on contention
// (unlike the previous fail-open pattern in both callers that let
// a track switch slip through when the mutex was busy past 50 ms).
//
// Caller is responsible for calling lap_timer_set_track(out_copy)
// after this returns OK — that call must happen OUTSIDE the mutex
// because lap_timer_set_track internally re-takes session_mutex.
// The out_copy is a caller-owned snapshot independent of the shared
// s_tracks[] array, so a concurrent track_delete on another task
// cannot invalidate the copy.
typedef enum {
    SESSION_TRACK_SWITCH_OK = 0,
    SESSION_TRACK_SWITCH_CONTENDED,  // session_mutex timeout
    SESSION_TRACK_SWITCH_RECORDING,  // refused because a session is active
    SESSION_TRACK_SWITCH_NO_FIX,     // autodetect requested but no 3D fix
    SESSION_TRACK_SWITCH_NOT_FOUND,  // id not in track store / no nearby track
} SessionTrackSwitchResult;

// If `is_auto` is true, the helper uses the current GPS fix (read
// under session_mutex) to find the nearest track.  If false, it
// looks up `id` in the track store.  On success, fills *out_copy
// with a caller-owned TrackDefinition and updates
// session_state.track_name.
SessionTrackSwitchResult session_try_claim_track_switch(
    bool is_auto, const char* id, TrackDefinition* out_copy);
