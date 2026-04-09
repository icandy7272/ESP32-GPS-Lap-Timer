// ============================================================
// /api/tracks handlers — list, create, select, delete
// ============================================================

#include "wifi_internal.h"
#include "track.h"
#include "session.h"
#include "lap_timer.h"
#include "types.h"

#include <Arduino.h>
#include <WebServer.h>
#include <SD.h>

// ============================================================
// GET /api/tracks — list track definitions
// ============================================================

static String extract_track_id(const char* filename);
static String extract_track_name(File& f);

void handle_api_tracks() {
    if (is_throttled()) {
        server.send(503, "application/json", "{\"error\":\"busy\"}");
        return;
    }

    String json = "{\"tracks\":[";
    bool first = true;

    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        File dir = SD.open("/tracks");
        if (dir && dir.isDirectory()) {
            File entry = dir.openNextFile();
            while (entry) {
                const char* name = entry.name();
                if (strstr(name, ".json") != nullptr) {
                    String id = extract_track_id(name);
                    String tname = extract_track_name(entry);
                    if (!first) { json += ","; }
                    json += "{\"id\":\"" + id + "\",\"name\":\"" + tname + "\"}";
                    first = false;
                }
                entry.close();
                entry = dir.openNextFile();
            }
            dir.close();
        }
        xSemaphoreGive(spi_mutex);
    }

    json += "]}";
    server.send(200, "application/json", json);
}

// Extract track ID from filename: "track_001.json" -> "track_001"
static String extract_track_id(const char* filename) {
    String s(filename);
    int dot = s.lastIndexOf('.');
    if (dot > 0) {
        return s.substring(0, dot);
    }
    return s;
}

// Read track name from first "name":"..." in a track JSON file.
// File cursor is at position 0. Reads up to 256 bytes.
static String extract_track_name(File& f) {
    char buf[256];
    size_t n = f.readBytes(buf, sizeof(buf) - 1);
    buf[n] = '\0';
    f.seek(0);

    char name[64] = "Unknown";
    // Reuse config.cpp-style extraction
    const char* pattern = "\"name\":\"";
    const char* start = strstr(buf, pattern);
    if (start) {
        start += strlen(pattern);
        const char* end = strchr(start, '"');
        if (end) {
            size_t len = (size_t)(end - start);
            if (len >= sizeof(name)) { len = sizeof(name) - 1; }
            memcpy(name, start, len);
            name[len] = '\0';
        }
    }
    return String(name);
}

// ============================================================
// POST /api/tracks — create new track definition
// ============================================================

/// Parse a float from JSON body by key name. Returns 0.0 if not found.
static double json_extract_double(const char* json, const char* key) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return 0.0;
    p = strchr(p + strlen(needle), ':');
    if (!p) return 0.0;
    return strtod(p + 1, nullptr);
}

/// Extract a string from JSON by key. Writes to out, returns false if missing.
bool json_extract_str(const char* json, const char* key,
                      char* out, int out_len) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return false;
    p = strchr(p + strlen(needle), '"');
    if (!p) return false;
    p++;  // skip opening quote
    const char* end = strchr(p, '"');
    if (!end) return false;
    int len = end - p;
    if (len >= out_len) len = out_len - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

static bool is_ascii_safe(const char* s) {
    for (int i = 0; s[i]; i++) {
        char c = s[i];
        if (c < 0x20 || c > 0x7E) return false;
        if (c == '"' || c == '\\' || c == '/' || c == ':' ||
            c == '*' || c == '?' || c == '<' || c == '>' || c == '|') {
            return false;
        }
    }
    return true;
}

void handle_api_tracks_post() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }

    String body = server.arg("plain");
    if (body.length() < 10 || body.length() > 2048) {
        server.send(400, "application/json", "{\"error\":\"invalid body size\"}");
        return;
    }

    const char* json = body.c_str();

    // Build a TrackDefinition from the web form JSON
    TrackDefinition track = {};

    if (!json_extract_str(json, "name", track.name, sizeof(track.name))) {
        server.send(400, "application/json", "{\"error\":\"missing name\"}");
        return;
    }
    if (!is_ascii_safe(track.name)) {
        server.send(400, "application/json",
                    "{\"error\":\"name contains invalid characters\"}");
        return;
    }

    // Start/finish line (required)
    track.start_finish.lat1_deg = json_extract_double(json, "sf_lat1");
    track.start_finish.lon1_deg = json_extract_double(json, "sf_lon1");
    track.start_finish.lat2_deg = json_extract_double(json, "sf_lat2");
    track.start_finish.lon2_deg = json_extract_double(json, "sf_lon2");
    track.start_finish.valid_heading_deg = (float)json_extract_double(json, "sf_heading");

    // Validate: start/finish must have non-zero coords
    if (track.start_finish.lat1_deg == 0.0 && track.start_finish.lon1_deg == 0.0) {
        server.send(400, "application/json", "{\"error\":\"missing start/finish coordinates\"}");
        return;
    }

    // Compute center from start/finish midpoint
    track.center_lat_deg = (track.start_finish.lat1_deg + track.start_finish.lat2_deg) / 2.0;
    track.center_lon_deg = (track.start_finish.lon1_deg + track.start_finish.lon2_deg) / 2.0;

    // Parse optional sectors array
    int sector_lines = 0;
    const char* arr = strstr(json, "\"sectors\"");
    if (arr) {
        const char* pos = strchr(arr, '[');
        if (pos) {
            pos++;
            // Iterate up to 3 sector objects
            for (int si = 0; si < MAX_SECTORS - 1 && sector_lines < MAX_SECTORS - 1; si++) {
                const char* obj = strchr(pos, '{');
                if (!obj) break;
                const char* obj_end = strchr(obj, '}');
                if (!obj_end) break;

                // Extract into a temporary null-terminated block
                int blen = (int)(obj_end - obj + 1);
                if (blen > 0 && blen < 512) {
                    char blk[512];
                    memcpy(blk, obj, blen);
                    blk[blen] = '\0';

                    DetectionLine* sl = &track.sectors[sector_lines];
                    sl->lat1_deg = json_extract_double(blk, "lat1");
                    sl->lon1_deg = json_extract_double(blk, "lon1");
                    sl->lat2_deg = json_extract_double(blk, "lat2");
                    sl->lon2_deg = json_extract_double(blk, "lon2");
                    sl->valid_heading_deg = (float)json_extract_double(blk, "heading");

                    // Only count if coords are non-zero
                    if (sl->lat1_deg != 0.0 || sl->lon1_deg != 0.0) {
                        sector_lines++;
                    }
                }
                pos = obj_end + 1;
            }
        }
    }
    track.sector_count = sector_lines + 1;  // split lines + start/finish

    // Save through track module (validates, generates ID, writes proper JSON)
    if (track_save(&track)) {
        server.send(201, "application/json", "{\"ok\":true}");
    } else {
        server.send(500, "application/json", "{\"error\":\"save failed\"}");
    }
}

static int find_next_track_id() {
    int max_id = 0;

    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (!SD.exists("/tracks")) {
            SD.mkdir("/tracks");
        }
        File dir = SD.open("/tracks");
        if (dir && dir.isDirectory()) {
            File entry = dir.openNextFile();
            while (entry) {
                const char* name = entry.name();
                // Parse "track_NNN.json"
                const char* p = strstr(name, "track_");
                if (p) {
                    int id = atoi(p + 6);
                    if (id > max_id) { max_id = id; }
                }
                entry.close();
                entry = dir.openNextFile();
            }
            dir.close();
        }
        xSemaphoreGive(spi_mutex);
    }
    return max_id + 1;
}

static bool write_track_file(const char* path, const String& body) {
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }

    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        xSemaphoreGive(spi_mutex);
        return false;
    }

    size_t written = f.print(body);
    f.flush();
    f.close();
    xSemaphoreGive(spi_mutex);

    return (written > 0);
}

// ============================================================
// POST /api/tracks/select — set active track
// ============================================================

void handle_api_tracks_select() {
    // Block track switch during recording to prevent timing/export inconsistency
    if (session_state.is_recording) {
        server.send(409, "application/json",
                    "{\"error\":\"cannot switch track during recording\"}");
        return;
    }

    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }

    String body = server.arg("plain");
    const char* json = body.c_str();
    char id[32] = {};
    if (!json_extract_str(json, "id", id, sizeof(id))) {
        server.send(400, "application/json", "{\"error\":\"missing id\"}");
        return;
    }

    const TrackDefinition* track = track_get_by_id(id);
    if (!track) {
        server.send(404, "application/json", "{\"error\":\"track not found\"}");
        return;
    }

    // Update lap timer with new track (copies data, resets state)
    lap_timer_set_track(track);

    // Update session state track name
    if (xSemaphoreTake(session_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        strlcpy(session_state.track_name, track->name,
                sizeof(session_state.track_name));
        xSemaphoreGive(session_mutex);
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"name\":\"%s\"}", track->name);
    server.send(200, "application/json", buf);
}

// ============================================================
// POST /api/tracks/delete — delete a track
// ============================================================

void handle_api_tracks_delete() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }

    String body = server.arg("plain");
    const char* json = body.c_str();
    char id[32] = {};
    if (!json_extract_str(json, "id", id, sizeof(id))) {
        server.send(400, "application/json", "{\"error\":\"missing id\"}");
        return;
    }

    // Check if the deleted track is the currently active one
    extern TrackDefinition active_track;
    bool was_active = (strcmp(active_track.id, id) == 0);

    if (track_delete(id)) {
        // If the active track was deleted, clear it and reset lap timer
        if (was_active) {
            memset(&active_track, 0, sizeof(TrackDefinition));
            lap_timer_reset();
            if (xSemaphoreTake(session_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                strlcpy(session_state.track_name, "No Track",
                        sizeof(session_state.track_name));
                xSemaphoreGive(session_mutex);
            }
        }
        server.send(200, "application/json", "{\"ok\":true}");
    } else {
        server.send(404, "application/json", "{\"error\":\"track not found\"}");
    }
}
