#include "lap_timer_internal.h"

#include "../delta.h"
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
    if (s_first_crossing) {
        s_first_crossing = false;
        s_lap_start_us = crossing_us;
        s_sector_start_us = crossing_us;
        s_current_sector = 0;
        delta_set_lap_start(crossing_us);
        delta_reset_elapsed();

        if (!session_state.is_recording) {
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

    int64_t elapsed_us = crossing_us - s_lap_start_us;
    int32_t lap_time_ms = (int32_t)(elapsed_us / 1000);

    bool is_new_best = false;
    if (is_lap_valid(lap_time_ms)) {
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
