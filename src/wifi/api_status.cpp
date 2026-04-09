// ============================================================
// GET /api/status
// ============================================================

#include "wifi_internal.h"
#include "types.h"

#include <Arduino.h>
#include <WebServer.h>

void handle_api_status() {
    if (is_throttled()) {
        server.send(503, "application/json", "{\"error\":\"busy\"}");
        return;
    }

    SessionState ss = read_session_state();

    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"gps_fix\":%s,"
             "\"satellites\":%d,"
             "\"recording\":%s,"
             "\"current_lap\":%d,"
             "\"best_lap_ms\":%ld,"
             "\"track\":\"%s\"}",
             ss.gps_fix_ok ? "true" : "false",
             ss.gps_satellites,
             ss.is_recording ? "true" : "false",
             ss.current_lap,
             (long)ss.best_lap_time_ms,
             ss.track_name);

    server.send(200, "application/json", buf);
}
