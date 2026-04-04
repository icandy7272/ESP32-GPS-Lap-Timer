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
void session_stop_recording();
