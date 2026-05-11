// ============================================================
// Session state machine — ESP32-S3 GPS Lap Timer
// ============================================================

#include <Arduino.h>
#include <string.h>

#include "session.h"
#include "storage.h"
#include "lap_timer.h"
#include "track.h"

// --- Shared globals ---

SessionState      session_state;
SemaphoreHandle_t session_mutex = nullptr;

// --- Module-private state ---

static SessionPhase   s_phase = SESSION_READY;
static QueueHandle_t  s_lap_event_q   = nullptr;
static QueueHandle_t  s_btn_session_q = nullptr;
static int64_t        s_lap_start_us  = 0;   // crossing timestamp of current lap start
static int64_t        s_sector_start_us[MAX_SECTORS] = {};
static int            s_current_sector = 0;

// --- Constants ---

// Stays in sync with lap_timer_internal.h MIN_LAP_TIME_MS via the shared
// WALKING_TEST_MODE macro. See docs/TEST_MODES.md for the full matrix.
#ifdef WALKING_TEST_MODE
static constexpr int32_t  LAP_SHORT_THRESHOLD_MS   = 8000;    // walking test (bumped 5→8s on 2026-04-18)
#else
static constexpr int32_t  LAP_SHORT_THRESHOLD_MS   = 15000;   // < 15 s
#endif
static constexpr int32_t  SECTOR_MIN_TIME_MS       = 5000;    // < 5 s = GPS jitter
static constexpr int32_t  LAP_SLOW_MULTIPLIER_150  = 150;     // > best * 1.5
static constexpr TickType_t QUEUE_POLL_TICKS = pdMS_TO_TICKS(50);

// --- Forward declarations ---

static void handle_lap_finish(const LapEvent* ev);
static void handle_lap_sector(const LapEvent* ev);
static void handle_button_press(const ButtonEvent* ev);
static void reset_session_state();
static LapStatus classify_lap(int32_t lap_time_ms);

// --- Public API ---

void session_init(QueueHandle_t lap_event_q, QueueHandle_t btn_session_q)
{
    session_mutex   = xSemaphoreCreateMutex();
    s_lap_event_q   = lap_event_q;
    s_btn_session_q = btn_session_q;
    reset_session_state();
}

void session_task(void* param)
{
    (void)param;
    LapEvent    lap_ev;
    ButtonEvent btn_ev;

    for (;;) {
        // Alternate polling: lap events first, then button events.
        if (xQueueReceive(s_lap_event_q, &lap_ev, QUEUE_POLL_TICKS) == pdTRUE) {
            // Accept lap events when recording (either via button/web or auto-start)
            if (s_phase != SESSION_RECORDING && !session_state.is_recording) {
                continue;  // ignore lap events when truly not recording
            }
            // Sync s_phase if auto-start activated recording outside session_start_recording()
            if (s_phase != SESSION_RECORDING && session_state.is_recording) {
                s_phase = SESSION_RECORDING;
            }
            if (lap_ev.event_type == LAP_EVENT_FINISH) {
                handle_lap_finish(&lap_ev);
            } else if (lap_ev.event_type == LAP_EVENT_SECTOR) {
                handle_lap_sector(&lap_ev);
            }
        }

        if (xQueueReceive(s_btn_session_q, &btn_ev, 0) == pdTRUE) {
            handle_button_press(&btn_ev);
        }
    }
}

void session_start_recording(const char* track_name)
{
    if (s_phase != SESSION_READY && s_phase != SESSION_FINISHED) {
        return;
    }

    // Attempt storage first.  If it fails, leave SessionState untouched
    // so session_stopped (and everything else) keeps whatever value the
    // user left us with — otherwise a failed start would silently clear
    // the manual-stop suppression and the next crossing would try to
    // auto-open a session again, hitting the same SD error in a loop.
    if (!storage_start_session(track_name)) {
        Serial.println("[session] SD failed — cannot record");
        return;
    }

    reset_session_state();
    lap_timer_reset();  // clear arming, history, best lap, delta reference

    xSemaphoreTake(session_mutex, portMAX_DELAY);
    session_state.is_recording = true;
    // Storage is up, so a real new session has started — unlock the
    // lap-timer auto-start path.  reset_session_state() already zeros
    // the struct, but spell it out for clarity.
    session_state.session_stopped = false;
    strncpy(session_state.track_name, track_name,
            sizeof(session_state.track_name) - 1);
    session_state.track_name[sizeof(session_state.track_name) - 1] = '\0';
    xSemaphoreGive(session_mutex);

    s_phase = SESSION_RECORDING;

    Serial.println("[session] recording started");
}

bool session_stop_recording()
{
    // Milestone log so long-session stop hangs can be diagnosed from
    // the boot/serial log alone.  2026-04-22 walking test: short
    // recordings save fine, a 10-minute session hung with LCD rec=1
    // and zero serial output from either session_stop_recording or
    // storage_end_session.  Without per-step markers there is no way
    // to tell whether the command reached the handler, which mutex
    // it's stuck on, or how far storage_end_session got before
    // blocking on SD.  Each milestone emits the current uptime in
    // ms so off-device analysis can compute delta-times between
    // steps.  Cheap — ~8 lines per stop event.
    Serial.printf("[stop-trace] session_stop: entry t=%lu\n",
                  (unsigned long)millis());

    bool was_recording = false;
    xSemaphoreTake(session_mutex, portMAX_DELAY);
    was_recording = session_state.is_recording;
    xSemaphoreGive(session_mutex);

    Serial.printf("[stop-trace] session_stop: mutex1_ok was_recording=%d "
                  "phase=%d t=%lu\n",
                  was_recording ? 1 : 0, (int)s_phase,
                  (unsigned long)millis());

    if (s_phase != SESSION_RECORDING && !was_recording) {
        Serial.printf("[stop-trace] session_stop: not_recording_exit t=%lu\n",
                      (unsigned long)millis());
        return false;
    }

    if (s_phase != SESSION_RECORDING && was_recording) {
        Serial.printf("[session] WARN: stop requested while phase=%d; "
                      "resyncing to recording state\n",
                      (int)s_phase);
    }

    xSemaphoreTake(session_mutex, portMAX_DELAY);
    session_state.is_recording = false;
    // Mark that the user explicitly stopped.  handle_finish_crossing()
    // reads this to reject crossings after a manual stop — including
    // the edge case where stop happens DURING the out lap (before any
    // start/finish crossing), where s_first_crossing is still true.
    session_state.session_stopped = true;
    // Clear the running-timer timestamp so the driving screen's
    // top-right elapsed-time field returns to 0:00.00.  The
    // complementary guard in update_session_delta() stops the delta
    // engine from echoing stale s_lap_start_us back while recording is
    // off, so this zero sticks.
    session_state.current_lap_start_us = 0;
    xSemaphoreGive(session_mutex);

    Serial.printf("[stop-trace] session_stop: is_recording_cleared "
                  "t=%lu\n", (unsigned long)millis());

    // NOTE: do NOT call lap_timer_reset() here.  Resetting would set
    // s_first_crossing back to true, which means the next start/finish
    // crossing would re-trigger handle_finish_crossing()'s first-crossing
    // branch and auto-call storage_start_session(), silently re-enabling
    // recording after the user explicitly stopped.  handle_finish_crossing()
    // now guards the auto-start path on SESSION_PHASE so crossings are
    // ignored while phase == SESSION_FINISHED.

    Serial.printf("[stop-trace] session_stop: calling_end_session t=%lu\n",
                  (unsigned long)millis());
    bool saved = storage_end_session();
    Serial.printf("[stop-trace] session_stop: end_session_returned "
                  "saved=%d t=%lu\n",
                  saved ? 1 : 0, (unsigned long)millis());
    s_phase = SESSION_FINISHED;

    if (saved) {
        Serial.println("[session] recording stopped");
    } else {
        Serial.println("[session] WARN: recording stopped but final save failed");
    }
    return saved;
}

// --- Private helpers ---

static void reset_session_state()
{
    xSemaphoreTake(session_mutex, portMAX_DELAY);
    memset(&session_state, 0, sizeof(session_state));
    session_state.current_lap      = 0;
    session_state.best_lap_number  = -1;
    session_state.best_lap_time_ms = -1;
    session_state.delta_ms         = 0;
    session_state.delta_valid      = false;
    session_state.is_recording     = false;
    session_state.lap_count        = 0;
    xSemaphoreGive(session_mutex);

    s_lap_start_us = 0;
}

static LapStatus classify_lap(int32_t lap_time_ms)
{
    if (lap_time_ms < LAP_SHORT_THRESHOLD_MS) {
        return LAP_STATUS_SHORT;
    }
    if (session_state.best_lap_time_ms < 0) {
        return LAP_STATUS_NO_REF;  // first valid lap
    }
    int32_t slow_threshold =
        (session_state.best_lap_time_ms * LAP_SLOW_MULTIPLIER_150) / 100;
    if (lap_time_ms > slow_threshold) {
        return LAP_STATUS_SLOW;
    }
    return LAP_STATUS_TIMED;
}

// Core 1 breadcrumb — shares RTC variable with lap_timer_events.cpp (Core 0)
extern int crash_bc_core1;

static void handle_lap_finish(const LapEvent* ev)
{
    crash_bc_core1 = 30;  // session: entering handle_lap_finish

    // First crossing sets the start reference
    if (s_lap_start_us == 0) {
        s_lap_start_us = ev->crossing_us;
        s_sector_start_us[0] = ev->crossing_us;
        s_current_sector = 0;
        xSemaphoreTake(session_mutex, portMAX_DELAY);
        session_state.current_lap = 1;
        // Init sector times for the first in-progress lap
        int first_idx = session_state.lap_count;
        if (first_idx < MAX_LAPS_PER_SESSION) {
            for (int i = 0; i < MAX_SECTORS; i++) {
                session_state.laps[first_idx].sector_times_ms[i] = -1;
            }
        }
        xSemaphoreGive(session_mutex);
        return;
    }

    crash_bc_core1 = 31;  // session: computing lap time

    int32_t lap_time_ms =
        (int32_t)((ev->crossing_us - s_lap_start_us) / 1000);

    crash_bc_core1 = 32;  // session: taking session_mutex
    xSemaphoreTake(session_mutex, portMAX_DELAY);

    int idx = session_state.lap_count;
    if (idx >= MAX_LAPS_PER_SESSION) {
        xSemaphoreGive(session_mutex);
        return;  // lap buffer full
    }

    LapStatus status = classify_lap(lap_time_ms);

    // Build lap record — preserve sector times already written by
    // handle_lap_sector() during this lap; only fill remaining slots.
    LapRecord* lap        = &session_state.laps[idx];
    lap->lap_number       = session_state.current_lap;
    lap->lap_time_ms      = lap_time_ms;
    lap->status           = (uint8_t)status;
    lap->finish_timestamp_us = ev->crossing_us;

    // Compute final sector time (from last sector start to finish line)
    lap->sector_times_ms[s_current_sector] =
        (int32_t)((ev->crossing_us - s_sector_start_us[s_current_sector]) / 1000);
    lap->sector_count = s_current_sector + 1;

    // Mark any unused sector slots as incomplete
    for (int i = lap->sector_count; i < MAX_SECTORS; i++) {
        lap->sector_times_ms[i] = -1;
    }

    session_state.lap_count++;
    session_state.current_lap++;

    // Update best lap (only TIMED and NO_REF qualify)
    if (status == LAP_STATUS_TIMED || status == LAP_STATUS_NO_REF) {
        if (session_state.best_lap_time_ms < 0 ||
            lap_time_ms < session_state.best_lap_time_ms) {
            session_state.best_lap_number  = lap->lap_number;
            session_state.best_lap_time_ms = lap_time_ms;
        }
    }

    // Init sector times for the next in-progress lap (still under mutex)
    int next_idx = session_state.lap_count;
    if (next_idx < MAX_LAPS_PER_SESSION) {
        for (int i = 0; i < MAX_SECTORS; i++) {
            session_state.laps[next_idx].sector_times_ms[i] = -1;
        }
    }

    // Snapshot the record before releasing the mutex — `lap` points into
    // shared session_state storage, and storage_write_lap_timing runs
    // outside the lock (SD I/O may block).  Without this copy, any future
    // non-stub implementation would race with writes to session_state.laps.
    LapRecord lap_snapshot = *lap;

    xSemaphoreGive(session_mutex);
    crash_bc_core1 = 33;  // session: mutex released, about to write lap

    // Persist lap to SD (outside mutex)
    storage_write_lap_timing(&lap_snapshot);
    crash_bc_core1 = 34;  // session: handle_lap_finish done

    // Advance lap start to this crossing; reset sector tracking
    s_lap_start_us = ev->crossing_us;
    s_sector_start_us[0] = ev->crossing_us;
    s_current_sector = 0;
}

static void handle_lap_sector(const LapEvent* ev)
{
    int completed = ev->sector_index - 1;
    if (completed < 0 || completed >= MAX_SECTORS) {
        return;  // invalid sector index
    }

    int64_t crossing_us = ev->crossing_us;

    // Compute time for the sector that just ended
    int32_t sector_ms =
        (int32_t)((crossing_us - s_sector_start_us[completed]) / 1000);

    // Ignore sector crossing if time is below minimum (GPS jitter)
    if (sector_ms < SECTOR_MIN_TIME_MS) {
        return;
    }

    // Start timing the next sector
    if (ev->sector_index < MAX_SECTORS) {
        s_sector_start_us[ev->sector_index] = crossing_us;
    }
    s_current_sector = ev->sector_index;

    // Write sector time into the current in-progress lap record
    xSemaphoreTake(session_mutex, portMAX_DELAY);
    int idx = session_state.lap_count;  // current lap being built
    if (idx >= 0 && idx < MAX_LAPS_PER_SESSION) {
        session_state.laps[idx].sector_times_ms[completed] = sector_ms;
    }
    xSemaphoreGive(session_mutex);
}

static void handle_button_press(const ButtonEvent* ev)
{
    if (ev->button_id != BUTTON_RECORD) {
        return;
    }
    if (ev->event_type != BUTTON_SHORT_PRESS) {
        return;
    }

    // Toggle recording
    if (s_phase == SESSION_READY || s_phase == SESSION_FINISHED) {
        // Use the currently-active track name rather than the literal
        // "Unknown Track" placeholder.  Field test 2026-05-11 found
        // that the previous hardcoded "Unknown Track" string clobbered
        // session_state.track_name (via session_start_recording ->
        // strncpy at session.cpp:113) so after the user pressed REC the
        // display switched from the loaded track to "Unknown Track" and
        // never came back — there is no code path that restores it on
        // stop.  active_track is the source of truth for the loaded
        // (or auto-detected) track; fall back to the placeholder only
        // when no track is actually loaded.
        extern TrackDefinition active_track;
        const char* name = active_track.name;
        if (name == nullptr || name[0] == '\0' ||
            strcmp(name, "No Track") == 0) {
            name = "Unknown Track";
        }
        session_start_recording(name);
    } else if (s_phase == SESSION_RECORDING) {
        (void)session_stop_recording();
    }
}

// --- Atomic track-switch claim (serial + HTTP shared helper) -------
//
// Semantics + rationale are in session.h.  Key points:
//   - 1000 ms mutex timeout with fail-closed return so a contended
//     session_mutex can't let a switch slip through DURING an
//     active recording.
//   - Recording check, track lookup/autodetect, and
//     session_state.track_name update ALL happen inside the same
//     critical section.  The previous HTTP and serial flows did
//     these as separate lock takes, leaving a window where a
//     concurrent recording_start could capture the stale name
//     while lap_timer was about to take the new geometry.
//   - Track lookup goes through track_copy_by_id /
//     track_auto_detect_copy which each take track_store_mutex
//     internally, so a concurrent track_delete shifting s_tracks[]
//     cannot invalidate the pointer mid-copy.  Lock ordering:
//     session_mutex (outer) → track_store_mutex (inner).  Never
//     reversed.
SessionTrackSwitchResult session_try_claim_track_switch(
    bool is_auto, const char* id, TrackDefinition* out_copy) {
    if (out_copy == nullptr) {
        return SESSION_TRACK_SWITCH_NOT_FOUND;
    }

    if (xSemaphoreTake(session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return SESSION_TRACK_SWITCH_CONTENDED;
    }

    if (session_state.is_recording) {
        xSemaphoreGive(session_mutex);
        return SESSION_TRACK_SWITCH_RECORDING;
    }

    if (is_auto) {
        if (!session_state.gps_fix_ok) {
            xSemaphoreGive(session_mutex);
            return SESSION_TRACK_SWITCH_NO_FIX;
        }
        double lat = session_state.gps_lat_deg;
        double lon = session_state.gps_lon_deg;
        TrackLookupResult r = track_auto_detect_copy(lat, lon, out_copy);
        if (r != TRACK_LOOKUP_OK) {
            xSemaphoreGive(session_mutex);
            // BUSY: track_store_mutex timeout (distinct from a
            // genuine no-track-nearby).  Map to CONTENDED so the
            // HTTP handler returns 503 and the operator retries,
            // not a misleading 404 "no nearby track" (codex review
            // 2026-04-22 round 4 Medium).
            return (r == TRACK_LOOKUP_BUSY)
                       ? SESSION_TRACK_SWITCH_CONTENDED
                       : SESSION_TRACK_SWITCH_NOT_FOUND;
        }
    } else {
        if (id == nullptr) {
            xSemaphoreGive(session_mutex);
            return SESSION_TRACK_SWITCH_NOT_FOUND;
        }
        TrackLookupResult r = track_copy_by_id(id, out_copy);
        if (r != TRACK_LOOKUP_OK) {
            xSemaphoreGive(session_mutex);
            return (r == TRACK_LOOKUP_BUSY)
                       ? SESSION_TRACK_SWITCH_CONTENDED
                       : SESSION_TRACK_SWITCH_NOT_FOUND;
        }
    }

    // Update session_state.track_name NOW, inside the same critical
    // section that confirmed is_recording=false, so a concurrent
    // recording_start can't capture the stale name while lap_timer
    // is about to take the new geometry.
    strlcpy(session_state.track_name, out_copy->name,
            sizeof(session_state.track_name));

    xSemaphoreGive(session_mutex);
    return SESSION_TRACK_SWITCH_OK;
}
