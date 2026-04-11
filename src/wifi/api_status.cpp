// ============================================================
// GET /api/status
// Reads individual fields from session_state under mutex
// to avoid copying the full ~4KB SessionState onto the wifi stack.
// ============================================================

#include "wifi_internal.h"
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

    char buf[320];
    snprintf(buf, sizeof(buf),
             "{\"gps_fix\":%s,"
             "\"satellites\":%d,"
             "\"lat\":%.7f,"
             "\"lon\":%.7f,"
             "\"recording\":%s,"
             "\"current_lap\":%d,"
             "\"best_lap_ms\":%ld,"
             "\"track\":\"%s\"}",
             gps_fix ? "true" : "false",
             sats,
             lat,
             lon,
             recording ? "true" : "false",
             cur_lap,
             (long)best_ms,
             track);

    server.send(200, "application/json", buf);
}
