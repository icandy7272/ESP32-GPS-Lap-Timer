// ============================================================
// POST /api/recording — start/stop recording
// Reads individual fields under mutex to avoid 4KB stack copy.
// ============================================================

#include "wifi_internal.h"
#include "session.h"
#include "types.h"

#include <Arduino.h>
#include <WebServer.h>
#include <string.h>

void handle_api_recording() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }

    String body = server.arg("plain");
    const char* json = body.c_str();
    char action[16] = {};
    if (!json_extract_str(json, "action", action, sizeof(action))) {
        server.send(400, "application/json", "{\"error\":\"missing action\"}");
        return;
    }

    if (strcmp(action, "start") == 0) {
        // Read only track_name — no full SessionState copy
        char track_name[64] = {0};
        if (xSemaphoreTake(session_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            memcpy(track_name, session_state.track_name, sizeof(track_name));
            xSemaphoreGive(session_mutex);
        }

        session_start_recording(track_name);

        // Verify recording started — read only is_recording
        bool started = false;
        if (xSemaphoreTake(session_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            started = session_state.is_recording;
            xSemaphoreGive(session_mutex);
        }

        if (!started) {
            server.send(500, "application/json",
                        "{\"ok\":false,\"error\":\"SD storage failed\"}");
        } else {
            server.send(200, "application/json",
                        "{\"ok\":true,\"recording\":true}");
        }
    } else if (strcmp(action, "stop") == 0) {
        session_stop_recording();
        server.send(200, "application/json",
                    "{\"ok\":true,\"recording\":false}");
    } else {
        server.send(400, "application/json",
                    "{\"error\":\"action must be start or stop\"}");
    }
}
