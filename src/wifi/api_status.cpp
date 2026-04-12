// ============================================================
// GET /api/status
// Reads individual fields from session_state under mutex
// to avoid copying the full ~4KB SessionState onto the wifi stack.
// ============================================================

#include "wifi_internal.h"
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

    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"gps_fix\":%s,"
             "\"satellites\":%d,"
             "\"lat\":%.7f,"
             "\"lon\":%.7f,"
             "\"recording\":%s,"
             "\"current_lap\":%d,"
             "\"best_lap_ms\":%ld,"
             "\"track\":\"%s\","
             "\"track_id\":\"%s\","
             "\"track_source\":\"%s\","
             "\"track_locked_manual\":%s,"
             "\"recording_cta_state\":\"%s\","
             "\"recording_cta_reason\":\"%s\"}",
             gps_fix ? "true" : "false",
             sats,
             lat,
             lon,
             recording ? "true" : "false",
             cur_lap,
             (long)best_ms,
             track,
             runtime_status.track_id,
             runtime_status.track_source,
             runtime_status.track_locked_manual ? "true" : "false",
             runtime_status.recording_cta_state,
             runtime_status.recording_cta_reason);

    server.send(200, "application/json", buf);
}
