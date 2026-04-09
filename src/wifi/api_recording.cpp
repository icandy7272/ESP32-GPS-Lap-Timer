// ============================================================
// POST /api/recording — start/stop recording
// ============================================================

#include "wifi_internal.h"
#include "session.h"
#include "types.h"

#include <Arduino.h>
#include <WebServer.h>

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
        SessionState ss = read_session_state();
        session_start_recording(ss.track_name);
        // Verify recording actually started (storage may have failed)
        SessionState after = read_session_state();
        if (!after.is_recording) {
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
