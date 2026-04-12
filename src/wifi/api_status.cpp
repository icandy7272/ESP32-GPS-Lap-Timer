// ============================================================
// GET /api/status
// Reads individual fields from session_state under mutex
// to avoid copying the full ~4KB SessionState onto the wifi stack.
// ============================================================

#include "wifi_internal.h"
#include "track.h"
#include "track_runtime.h"
#include "types.h"

#include <Arduino.h>
#include <WebServer.h>
#include <string.h>

void handle_api_status() {
    if (is_throttled()) {
        server.send(503, "application/json", "{\"error\":\"busy\"}");
        return;
    }

    // Read only the fields we need — avoids 4KB stack copy
    bool     gps_fix   = false;
    int      sats      = 0;
    double   lat       = 0.0;
    double   lon       = 0.0;
    bool     recording = false;
    int      cur_lap   = 0;
    int32_t  best_ms   = -1;
    char     track[64] = {0};
    TrackRuntimeStatus runtime_status = {};
    extern TrackDefinition active_track;
    TrackDefinition active_track_snapshot = {};
    NearbyTrackCandidate nearby_candidates[MAX_NEARBY_TRACKS + 1] = {};
    double current_track_distance_m = -1.0;
    int nearby_count = 0;

    if (xSemaphoreTake(session_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        gps_fix   = session_state.gps_fix_ok;
        sats      = session_state.gps_satellites;
        lat       = session_state.gps_lat_deg;
        lon       = session_state.gps_lon_deg;
        recording = session_state.is_recording;
        cur_lap   = session_state.current_lap;
        best_ms   = session_state.best_lap_time_ms;
        memcpy(track, session_state.track_name, sizeof(track));
        xSemaphoreGive(session_mutex);
    }
    memcpy(&active_track_snapshot, &active_track, sizeof(active_track_snapshot));
    track_runtime_fill_status(&active_track_snapshot, track, recording,
                              &runtime_status);
    if (gps_fix) {
        if (track_runtime_has_valid_selected_track(&active_track_snapshot, track)) {
            current_track_distance_m =
                track_distance_to_center_m(&active_track_snapshot, lat, lon);
        }
        int raw_nearby_count = track_find_nearby(
            lat, lon, nearby_candidates, MAX_NEARBY_TRACKS + 1);
        for (int i = 0; i < raw_nearby_count && nearby_count < MAX_NEARBY_TRACKS; i++) {
            if (!nearby_candidates[i].track) {
                continue;
            }
            if (runtime_status.track_id[0] != '\0'
                && strcmp(runtime_status.track_id,
                          nearby_candidates[i].track->id) == 0) {
                continue;
            }
            nearby_candidates[nearby_count++] = nearby_candidates[i];
        }
    }

    String json = "{";
    json += "\"gps_fix\":";
    json += gps_fix ? "true" : "false";
    json += ",\"satellites\":";
    json += String(sats);
    json += ",\"lat\":";
    json += String(lat, 7);
    json += ",\"lon\":";
    json += String(lon, 7);
    json += ",\"recording\":";
    json += recording ? "true" : "false";
    json += ",\"current_lap\":";
    json += String(cur_lap);
    json += ",\"best_lap_ms\":";
    json += String((long)best_ms);
    json += ",\"track\":\"";
    json += track;
    json += "\",\"track_id\":\"";
    json += runtime_status.track_id;
    json += "\",\"track_source\":\"";
    json += runtime_status.track_source;
    json += "\",\"track_locked_manual\":";
    json += runtime_status.track_locked_manual ? "true" : "false";
    json += ",\"recording_cta_state\":\"";
    json += runtime_status.recording_cta_state;
    json += "\",\"recording_cta_reason\":\"";
    json += runtime_status.recording_cta_reason;
    json += "\",\"current_track_distance_m\":";
    json += current_track_distance_m >= 0.0
        ? String((long)(current_track_distance_m + 0.5))
        : String(-1);
    json += ",\"nearby_tracks\":[";
    for (int i = 0; i < nearby_count; i++) {
        if (i > 0) {
            json += ",";
        }
        json += "{\"id\":\"";
        json += nearby_candidates[i].track->id;
        json += "\",\"name\":\"";
        json += nearby_candidates[i].track->name;
        json += "\",\"distance_m\":";
        json += String((long)(nearby_candidates[i].distance_m + 0.5));
        json += "}";
    }
    json += "]}";

    server.send(200, "application/json", json);
}
