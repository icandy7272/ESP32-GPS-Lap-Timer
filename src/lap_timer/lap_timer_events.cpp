#include "lap_timer_internal.h"

#include "../delta.h"
#include "../lap_timer_guard.h"
#include "../session.h"
#include "../storage.h"

#include <Arduino.h>

// Per-core breadcrumbs — survive warm resets, cleared on power cycle.
// Core 0: lap_timer, gps     Core 1: session, display, storage, wifi
RTC_NOINIT_ATTR int crash_bc_core0;
RTC_NOINIT_ATTR int crash_bc_core1;

namespace lap_timer_internal {

void emit_lap_event(uint8_t event_type, int sector_index, int64_t crossing_us) {
    LapEvent event;
    event.event_type = event_type;
    event.sector_index = sector_index;
    event.crossing_us = crossing_us;
    xQueueSend(s_lap_event_queue, &event, 0);
}

bool is_lap_valid(int32_t lap_time_ms) {
    if (lap_time_ms < MIN_LAP_TIME_MS) {
        return false;
    }
    if (s_best_lap_time_ms > 0
        && lap_time_ms > (int32_t)(s_best_lap_time_ms * MAX_LAP_RATIO)) {
        return false;
    }
    return true;
}

void handle_finish_crossing(int64_t crossing_us) {
    // Snapshot the two flags under the mutex so the guard below sees a
    // consistent pair.  session_stop_recording() writes is_recording and
    // session_stopped together under s_session_mutex; without taking the
    // same mutex here, Core 0 can observe them out-of-order on Xtensa LX7
    // (no stdlib memory-model guarantee, only empirical cache coherency),
    // miss the guard, and silently auto-start during a stop.
    bool is_recording;
    bool session_stopped;
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    is_recording    = session_state.is_recording;
    session_stopped = session_state.session_stopped;
    xSemaphoreGive(s_session_mutex);

    // Decide what this crossing means.  Pure truth table lives in
    // lap_timer_guard so it can be host-tested; this handler only
    // performs the side effects.  Reject covers both "stop AFTER first
    // crossing" and "stop DURING out lap"; the AutoStartSession branch
    // also runs the storage_start_session rollback on SD failure.
    CrossingDecision decision = classify_crossing(session_stopped,
                                                  is_recording,
                                                  s_first_crossing);

    if (decision == CrossingDecision::Reject) {
        Serial.printf("[lap] crossing ignored (recording stopped)\n");
        return;
    }

    if (decision == CrossingDecision::AutoStartSession
        || decision == CrossingDecision::StartLapTimer) {
        s_first_crossing = false;
        s_lap_start_us = crossing_us;
        s_sector_start_us = crossing_us;
        s_current_sector = 0;
        delta_set_lap_start(crossing_us);
        delta_reset_elapsed();
        Serial.printf("[lap] first crossing — timer started\n");

        if (decision == CrossingDecision::AutoStartSession) {
            const char* tname = session_state.track_name[0] != '\0'
                                    ? session_state.track_name
                                    : "Unknown Track";
            xSemaphoreTake(s_session_mutex, portMAX_DELAY);
            session_state.is_recording = true;
            xSemaphoreGive(s_session_mutex);
            if (!storage_start_session(tname)) {
                xSemaphoreTake(s_session_mutex, portMAX_DELAY);
                session_state.is_recording = false;
                xSemaphoreGive(s_session_mutex);
                Serial.println("[lap_timer] Auto-start failed — SD error");
            }
        }

        emit_lap_event(LAP_EVENT_FINISH, 0, crossing_us);
        return;
    }

    // decision == CompleteLap — fall through to normal lap timing below.

    int64_t elapsed_us = crossing_us - s_lap_start_us;
    int32_t lap_time_ms = (int32_t)(elapsed_us / 1000);
    bool valid = is_lap_valid(lap_time_ms);

    Serial.printf("[lap] finish crossing — lap_ms=%d valid=%d best=%d\n",
                  lap_time_ms, valid, s_best_lap_time_ms);

    bool is_new_best = false;
    if (valid) {
        if (s_best_lap_time_ms < 0 || lap_time_ms < s_best_lap_time_ms) {
            s_best_lap_time_ms = lap_time_ms;
            is_new_best = true;
        }
    }

    if (is_new_best && s_lap_points != nullptr && s_lap_point_count > 0) {
        crash_bc_core0 = 10;  // entering delta_set_reference
        delta_set_reference(s_lap_points, s_lap_point_count);
        crash_bc_core0 = 12;  // delta done
    }

    crash_bc_core0 = 13;  // about to emit lap event
    emit_lap_event(LAP_EVENT_FINISH, 0, crossing_us);

    s_lap_point_count = 0;
    s_lap_start_us = crossing_us;
    s_sector_start_us = crossing_us;
    s_current_sector = 0;
    delta_set_lap_start(crossing_us);
    delta_reset_elapsed();
}

void handle_sector_crossing(int line_idx, int64_t crossing_us) {
    int expected_sector = s_current_sector + 1;
    if (line_idx != expected_sector) {
        return;
    }

    emit_lap_event(LAP_EVENT_SECTOR, line_idx, crossing_us);
    s_sector_start_us = crossing_us;
    s_current_sector = line_idx;
}

}  // namespace lap_timer_internal
