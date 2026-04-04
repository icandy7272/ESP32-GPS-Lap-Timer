// ============================================================
// Session state machine — ESP32-S3 GPS Lap Timer
// ============================================================

#include <Arduino.h>
#include <string.h>

#include "session.h"
#include "storage.h"

// --- Shared globals ---

SessionState      session_state;
SemaphoreHandle_t session_mutex = nullptr;

// --- Module-private state ---

static SessionPhase   s_phase = SESSION_READY;
static QueueHandle_t  s_lap_event_q   = nullptr;
static QueueHandle_t  s_btn_session_q = nullptr;
static int64_t        s_lap_start_us  = 0;   // crossing timestamp of current lap start

// --- Constants ---

static constexpr int32_t  LAP_SHORT_THRESHOLD_MS   = 15000;   // < 15 s
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
            if (s_phase != SESSION_RECORDING) {
                continue;  // ignore lap events when not recording
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

    reset_session_state();

    xSemaphoreTake(session_mutex, portMAX_DELAY);
    session_state.is_recording = true;
    strncpy(session_state.track_name, track_name,
            sizeof(session_state.track_name) - 1);
    session_state.track_name[sizeof(session_state.track_name) - 1] = '\0';
    xSemaphoreGive(session_mutex);

    storage_start_session(track_name);
    s_phase = SESSION_RECORDING;

    Serial.println("[session] recording started");
}

void session_stop_recording()
{
    if (s_phase != SESSION_RECORDING) {
        return;
    }

    xSemaphoreTake(session_mutex, portMAX_DELAY);
    session_state.is_recording = false;
    xSemaphoreGive(session_mutex);

    storage_end_session();
    s_phase = SESSION_FINISHED;

    Serial.println("[session] recording stopped");
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

static void handle_lap_finish(const LapEvent* ev)
{
    // First crossing sets the start reference
    if (s_lap_start_us == 0) {
        s_lap_start_us = ev->crossing_us;
        xSemaphoreTake(session_mutex, portMAX_DELAY);
        session_state.current_lap = 1;
        xSemaphoreGive(session_mutex);
        return;
    }

    int32_t lap_time_ms =
        (int32_t)((ev->crossing_us - s_lap_start_us) / 1000);

    xSemaphoreTake(session_mutex, portMAX_DELAY);

    int idx = session_state.lap_count;
    if (idx >= MAX_LAPS_PER_SESSION) {
        xSemaphoreGive(session_mutex);
        return;  // lap buffer full
    }

    LapStatus status = classify_lap(lap_time_ms);

    // Build lap record
    LapRecord* lap        = &session_state.laps[idx];
    lap->lap_number       = session_state.current_lap;
    lap->lap_time_ms      = lap_time_ms;
    lap->sector_count     = 0;
    lap->status           = (uint8_t)status;
    lap->finish_timestamp_us = ev->crossing_us;

    // Init sector times to incomplete
    for (int i = 0; i < MAX_SECTORS; i++) {
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

    xSemaphoreGive(session_mutex);

    // Persist lap to SD (outside mutex)
    storage_write_lap_timing(lap);

    // Advance lap start to this crossing
    s_lap_start_us = ev->crossing_us;
}

static void handle_lap_sector(const LapEvent* ev)
{
    (void)ev;
    // Sector timing will be implemented with per-sector start
    // timestamps. For now, acknowledge the event.
    // TODO: track sector_start_us[] and compute sector_times_ms.
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
        session_start_recording("Unknown Track");
    } else if (s_phase == SESSION_RECORDING) {
        session_stop_recording();
    }
}
