#include "lap_timer_internal.h"

#include "../track.h"
#include "../track_runtime.h"
#include "../session.h"

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <string.h>

namespace {

void forward_vbo_entry(QueueHandle_t vbo_queue, const GpsPoint* pt) {
    VboEntry entry;
    entry.satellites = pt->satellites;
    entry.timestamp_us = pt->timestamp_us;
    entry.lat_deg = pt->lat_deg;
    entry.lon_deg = pt->lon_deg;
    entry.speed_kmh = pt->speed_kmh;
    entry.heading_deg = pt->heading_deg;
    entry.height_m = pt->height_m;
    xQueueSend(vbo_queue, &entry, 0);
}

}  // namespace

void lap_timer_task(void* param) {
    (void)param;

    using namespace lap_timer_internal;

    esp_task_wdt_add(NULL);

    GpsPoint curr;
    GpsPoint prev;
    bool has_prev = false;
    bool auto_detected = false;

    for (;;) {
        if (xQueueReceive(s_gps_queue, &curr, pdMS_TO_TICKS(200)) != pdTRUE) {
            esp_task_wdt_reset();
            continue;
        }

        if (!auto_detected && curr.fix_3d) {
            SessionState ss = {};
            if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                ss = session_state;
                xSemaphoreGive(s_session_mutex);
            }

            if (!track_runtime_should_apply_late_auto_detect(
                    ss.is_recording, ss.track_name)) {
                auto_detected = true;
                continue;
            }

            int candidates = 0;
            for (int ti = 0; ti < track_count(); ti++) {
                const TrackDefinition* t = track_get(ti);
                if (t) {
                    double d = haversine_m(curr.lat_deg, curr.lon_deg,
                                           t->center_lat_deg, t->center_lon_deg);
                    if (d < 5000.0) {
                        candidates++;
                    }
                }
            }

            const TrackDefinition* detected =
                track_auto_detect(curr.lat_deg, curr.lon_deg);

            if (detected != nullptr) {
                extern TrackDefinition active_track;
                char detected_name[sizeof(session_state.track_name)] = {0};
                if (track_runtime_sync_detected_track(
                        &active_track, detected,
                        detected_name, sizeof(detected_name))) {
                    s_track = &active_track;
                    lap_timer_reset();
                    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        strlcpy(session_state.track_name, detected_name,
                                sizeof(session_state.track_name));
                        xSemaphoreGive(s_session_mutex);
                    }
                    Serial.printf("[lap_timer] Auto-detected: %s (%d candidate(s) within 5km)\n",
                                  detected_name, candidates);
                }
            } else if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                strlcpy(session_state.track_name, "No Track",
                        sizeof(session_state.track_name));
                xSemaphoreGive(s_session_mutex);
                track_runtime_note_track_cleared();
                Serial.println("[lap_timer] WARN: No track within 5km — configure via phone");
            }

            auto_detected = true;
        }

        history_push(&curr);

        if (has_prev) {
            process_line(0, &s_track->start_finish, &prev, &curr);

            for (int i = 0; i < s_track->sector_count - 1; i++) {
                process_line(i + 1, &s_track->sectors[i], &prev, &curr);
            }

            update_session_delta(&curr);
        }

        if (s_lap_points != nullptr && s_lap_point_count < MAX_LAP_POINTS
            && !s_first_crossing) {
            s_lap_points[s_lap_point_count++] = curr;
        }

        forward_vbo_entry(s_vbo_queue, &curr);

        prev = curr;
        has_prev = true;
        esp_task_wdt_reset();
    }
}
