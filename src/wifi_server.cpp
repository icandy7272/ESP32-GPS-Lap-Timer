// ============================================================
// WiFi AP + HTTP server — ESP32-S3 GPS Lap Timer
// Provides: web dashboard, session/track API, VBO download,
// settings management.  Uses ESP32 WebServer library.
//
// Requires: WiFi.h, WebServer.h (Arduino ESP32 core)
// spi_mutex / session_mutex must be created before wifi_init().
// ============================================================

#include "wifi_server.h"
#include "config.h"
#include "track.h"
#include "session.h"
#include "lap_timer.h"
#include "types.h"
#include "pins.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>

// --- Extern references (created in main.cpp) ---
extern SemaphoreHandle_t spi_mutex;
extern SemaphoreHandle_t session_mutex;
extern SessionState      session_state;

// --- HTTP server on port 80 ---
static WebServer server(80);

// --- Recording-mode throttle ---
static unsigned long last_request_ms = 0;
static const unsigned long RECORDING_THROTTLE_MS = 500;

// --- File streaming constants ---
static const size_t FILE_CHUNK_SIZE       = 4096;
static const unsigned long CHUNK_DELAY_MS = 10;

// ============================================================
// Forward declarations
// ============================================================

static void handle_root();
static void handle_api_status();
static void handle_api_sessions();
static void handle_api_tracks();
static void handle_api_tracks_post();
static void handle_api_tracks_select();
static void handle_api_tracks_delete();
static void handle_api_recording();
static void handle_files();
static void handle_api_settings_get();
static void handle_api_settings_post();
static void handle_not_found();

static String build_dashboard_html();
static String build_head_section();
static String build_style_section();
static String build_body_section();
static String build_script_section();

static SessionState read_session_state();
static bool is_throttled();

static String extract_track_id(const char* filename);
static String extract_track_name(File& f);
static int find_next_track_id();
static bool write_track_file(const char* path, const String& body);
static bool validate_filename(const String& name);
static void stream_file_from_sd(const String& path, const String& filename);
static void apply_settings_from_json(const char* json);
static bool extract_json_string(const char* json, const char* key,
                                char* out, size_t out_size);
static bool extract_json_int(const char* json, const char* key, int* out);

// ============================================================
// Initialisation
// ============================================================

void wifi_init() {
    // Mount Arduino SD singleton (storage.cpp uses SdFat separately)
    // Both can coexist since they share SPI bus via spi_mutex
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    SD.begin(PIN_SD_CS);
    xSemaphoreGive(spi_mutex);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(app_config.wifi_ssid, app_config.wifi_pass);

    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[wifi] AP started: SSID=%s  IP=%s\n",
                  app_config.wifi_ssid, ip.toString().c_str());

    server.on("/",               HTTP_GET,  handle_root);
    server.on("/api/status",     HTTP_GET,  handle_api_status);
    server.on("/api/sessions",   HTTP_GET,  handle_api_sessions);
    server.on("/api/tracks",        HTTP_GET,  handle_api_tracks);
    server.on("/api/tracks",        HTTP_POST, handle_api_tracks_post);
    server.on("/api/tracks/select", HTTP_POST, handle_api_tracks_select);
    server.on("/api/tracks/delete", HTTP_POST, handle_api_tracks_delete);
    server.on("/api/recording",     HTTP_POST, handle_api_recording);
    server.on("/api/settings",      HTTP_GET,  handle_api_settings_get);
    server.on("/api/settings",      HTTP_POST, handle_api_settings_post);
    server.onNotFound(handle_not_found);

    server.begin();
    Serial.println("[wifi] HTTP server started on port 80");
}

// ============================================================
// FreeRTOS task
// ============================================================

void wifi_task(void* param) {
    (void)param;
    for (;;) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ============================================================
// Throttle helper — limit requests during recording
// ============================================================

static bool is_throttled() {
    SessionState ss = read_session_state();
    if (!ss.is_recording) {
        return false;
    }
    unsigned long now = millis();
    if (now - last_request_ms < RECORDING_THROTTLE_MS) {
        return true;
    }
    last_request_ms = now;
    return false;
}

// ============================================================
// Thread-safe session state snapshot
// ============================================================

static SessionState read_session_state() {
    SessionState ss;
    if (xSemaphoreTake(session_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        ss = session_state;
        xSemaphoreGive(session_mutex);
    } else {
        memset(&ss, 0, sizeof(ss));
    }
    return ss;
}

// ============================================================
// GET / — HTML dashboard
// ============================================================

static void handle_root() {
    if (is_throttled()) {
        server.send(503, "text/plain", "Recording in progress, try later");
        return;
    }
    String html = build_dashboard_html();
    server.send(200, "text/html", html);
}

// ============================================================
// GET /api/status
// ============================================================

static void handle_api_status() {
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

// ============================================================
// GET /api/sessions — list VBO files on SD
// ============================================================

static void handle_api_sessions() {
    if (is_throttled()) {
        server.send(503, "application/json", "{\"error\":\"busy\"}");
        return;
    }

    String json = "{\"sessions\":[";
    bool first = true;

    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        File dir = SD.open("/sessions");
        if (dir && dir.isDirectory()) {
            File entry = dir.openNextFile();
            while (entry) {
                const char* name = entry.name();
                if (strstr(name, ".vbo") != nullptr) {
                    if (!first) { json += ","; }
                    json += "\"";
                    json += name;
                    json += "\"";
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

// ============================================================
// GET /api/tracks — list track definitions
// ============================================================

static void handle_api_tracks() {
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
static bool json_extract_str(const char* json, const char* key,
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

static void handle_api_tracks_post() {
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

// ============================================================
// POST /api/tracks/select — set active track
// ============================================================

static void handle_api_tracks_select() {
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

static void handle_api_tracks_delete() {
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

    if (track_delete(id)) {
        server.send(200, "application/json", "{\"ok\":true}");
    } else {
        server.send(404, "application/json", "{\"error\":\"track not found\"}");
    }
}

// ============================================================
// POST /api/recording — start/stop recording
// ============================================================

static void handle_api_recording() {
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
        server.send(200, "application/json",
                    "{\"ok\":true,\"recording\":true}");
    } else if (strcmp(action, "stop") == 0) {
        session_stop_recording();
        server.send(200, "application/json",
                    "{\"ok\":true,\"recording\":false}");
    } else {
        server.send(400, "application/json",
                    "{\"error\":\"action must be start or stop\"}");
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
// GET /files/:name — stream VBO file for download
// ============================================================

static void handle_not_found() {
    String uri = server.uri();

    // Route: /files/filename.vbo
    if (uri.startsWith("/files/")) {
        handle_files();
        return;
    }
    server.send(404, "text/plain", "Not found");
}

static void handle_files() {
    String uri = server.uri();
    String filename = uri.substring(7); // strip "/files/"

    // Validate filename: only alphanumeric, underscore, dot, hyphen
    if (!validate_filename(filename)) {
        server.send(400, "text/plain", "Invalid filename");
        return;
    }

    String path = "/sessions/" + filename;
    stream_file_from_sd(path, filename);
}

static bool validate_filename(const String& name) {
    if (name.length() == 0 || name.length() > 64) {
        return false;
    }
    for (unsigned int i = 0; i < name.length(); i++) {
        char c = name.charAt(i);
        bool ok = (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  c == '_' || c == '.' || c == '-';
        if (!ok) { return false; }
    }
    return true;
}

static void stream_file_from_sd(const String& path,
                                const String& filename) {
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        server.send(503, "text/plain", "SD card busy");
        return;
    }

    File f = SD.open(path, FILE_READ);
    if (!f) {
        xSemaphoreGive(spi_mutex);
        server.send(404, "text/plain", "File not found");
        return;
    }

    size_t file_size = f.size();
    String disposition = "attachment; filename=" + filename;

    server.sendHeader("Content-Disposition", disposition);
    server.setContentLength(file_size);
    server.send(200, "application/octet-stream", "");

    // Stream in chunks with rate limiting during recording
    uint8_t buf[FILE_CHUNK_SIZE];
    while (f.available()) {
        size_t n = f.read(buf, sizeof(buf));
        if (n == 0) { break; }
        // VBO is text-based, safe to wrap in String
        String chunk;
        chunk.concat((const char*)buf, n);
        server.sendContent(chunk);
        vTaskDelay(pdMS_TO_TICKS(CHUNK_DELAY_MS));
    }

    f.close();
    xSemaphoreGive(spi_mutex);
}

// ============================================================
// GET /api/settings
// ============================================================

static void handle_api_settings_get() {
    if (is_throttled()) {
        server.send(503, "application/json", "{\"error\":\"busy\"}");
        return;
    }

    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"wifi_ssid\":\"%s\","
             "\"wifi_pass\":\"%s\","
             "\"brightness\":%u,"
             "\"gps_rate_hz\":%u}",
             app_config.wifi_ssid,
             app_config.wifi_pass,
             app_config.brightness,
             app_config.gps_rate_hz);

    server.send(200, "application/json", buf);
}

// ============================================================
// POST /api/settings
// ============================================================

static void handle_api_settings_post() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }

    String body = server.arg("plain");
    const char* json = body.c_str();

    apply_settings_from_json(json);

    // Apply brightness immediately (LEDC channel 0 = TFT backlight)
    ledcWrite(0, app_config.brightness);

    bool ok = config_save();
    if (ok) {
        server.send(200, "application/json", "{\"ok\":true}");
    } else {
        server.send(500, "application/json", "{\"error\":\"save failed\"}");
    }
}

static void apply_settings_from_json(const char* json) {
    // Extract string fields
    char tmp_str[32];
    if (extract_json_string(json, "wifi_ssid", tmp_str, sizeof(tmp_str))) {
        strlcpy(app_config.wifi_ssid, tmp_str, sizeof(app_config.wifi_ssid));
    }
    if (extract_json_string(json, "wifi_pass", tmp_str, sizeof(tmp_str))) {
        strlcpy(app_config.wifi_pass, tmp_str, sizeof(app_config.wifi_pass));
    }

    // Extract integer fields
    int val = 0;
    if (extract_json_int(json, "brightness", &val)) {
        app_config.brightness = (uint8_t)constrain(val, 0, 255);
    }
    if (extract_json_int(json, "gps_rate_hz", &val)) {
        app_config.gps_rate_hz = (uint8_t)constrain(val, 1, 25);
    }
}

// --- JSON extraction helpers (same logic as config.cpp) ---

static bool extract_json_string(const char* json,
                                const char* key,
                                char* out, size_t out_size) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);

    const char* start = strstr(json, pattern);
    if (!start) { return false; }
    start += strlen(pattern);

    const char* end = strchr(start, '"');
    if (!end) { return false; }

    size_t len = (size_t)(end - start);
    if (len >= out_size) { len = out_size - 1; }
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

static bool extract_json_int(const char* json,
                             const char* key,
                             int* out_value) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    const char* start = strstr(json, pattern);
    if (!start) { return false; }
    start += strlen(pattern);

    while (*start == ' ' || *start == '\t') { start++; }

    char* end_ptr = nullptr;
    long val = strtol(start, &end_ptr, 10);
    if (end_ptr == start) { return false; }

    *out_value = (int)val;
    return true;
}

// ============================================================
// HTML Dashboard builder (split into sections for readability)
// ============================================================

static String build_dashboard_html() {
    String html;
    html.reserve(4096);
    html += "<!DOCTYPE html><html lang=\"en\">";
    html += build_head_section();
    html += build_body_section();
    html += build_script_section();
    html += "</html>";
    return html;
}

static String build_head_section() {
    return "<head>"
           "<meta charset=\"UTF-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<title>GPS Lap Timer</title>"
           + build_style_section() +
           "</head>";
}

static String build_style_section() {
    return "<style>"
           "*{box-sizing:border-box;margin:0;padding:0}"
           "body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#eee;"
             "padding:16px;max-width:600px;margin:auto}"
           "h1{text-align:center;color:#0ff;margin-bottom:16px;font-size:1.4em}"
           ".card{background:#16213e;border-radius:8px;padding:12px;margin-bottom:12px}"
           ".card h2{font-size:1em;color:#0af;margin-bottom:8px}"
           ".row{display:flex;justify-content:space-between;padding:4px 0}"
           ".label{color:#888}.val{font-weight:bold}"
           ".ok{color:#0f0}.warn{color:#f80}.err{color:#f44}"
           "a{color:#0af;text-decoration:none}"
           "a:hover{text-decoration:underline}"
           "ul{list-style:none;padding:0}"
           "li{padding:4px 0;border-bottom:1px solid #223}"
           "input,select{background:#0d1b2a;color:#eee;border:1px solid #334;"
             "border-radius:4px;padding:6px 8px;width:100%;margin:4px 0}"
           "button{background:#0af;color:#000;border:none;border-radius:4px;"
             "padding:8px 16px;cursor:pointer;font-weight:bold;margin-top:8px}"
           "button:hover{background:#08d}"
           "#msg{text-align:center;color:#0f0;padding:8px;display:none}"
           "</style>";
}

static String build_body_section() {
    return "<body>"
           "<h1>GPS Lap Timer</h1>"

           // Status card
           "<div class=\"card\" id=\"status-card\">"
           "<h2>Status</h2>"
           "<div class=\"row\"><span class=\"label\">GPS Fix</span>"
             "<span class=\"val\" id=\"gps-fix\">--</span></div>"
           "<div class=\"row\"><span class=\"label\">Satellites</span>"
             "<span class=\"val\" id=\"sats\">--</span></div>"
           "<div class=\"row\"><span class=\"label\">Recording</span>"
             "<span class=\"val\" id=\"rec\">--</span></div>"
           "<div class=\"row\"><span class=\"label\">Current Lap</span>"
             "<span class=\"val\" id=\"lap\">--</span></div>"
           "<div class=\"row\"><span class=\"label\">Best Lap</span>"
             "<span class=\"val\" id=\"best\">--</span></div>"
           "<div class=\"row\"><span class=\"label\">Track</span>"
             "<span class=\"val\" id=\"track\">--</span></div>"
           "<button id=\"rec-btn\" onclick=\"toggleRecording()\" "
             "style=\"width:100%;padding:12px;font-size:1.1em;margin-top:8px\">"
             "Loading...</button>"
           "</div>"

           // Sessions card
           "<div class=\"card\">"
           "<h2>Sessions</h2>"
           "<ul id=\"sessions\"><li>Loading...</li></ul>"
           "</div>"

           // Tracks card
           "<div class=\"card\">"
           "<h2>Tracks</h2>"
           "<ul id=\"tracks\"><li>Loading...</li></ul>"
           "<h2 style=\"margin-top:12px\">Add Track</h2>"
           "<input id=\"tname\" placeholder=\"Track name\">"
           "<p style=\"color:#888;font-size:12px;margin:4px 0\">Start/Finish Line</p>"
           "<input id=\"tlat1\" placeholder=\"SF lat1\" type=\"number\" step=\"any\">"
           "<input id=\"tlon1\" placeholder=\"SF lon1\" type=\"number\" step=\"any\">"
           "<input id=\"tlat2\" placeholder=\"SF lat2\" type=\"number\" step=\"any\">"
           "<input id=\"tlon2\" placeholder=\"SF lon2\" type=\"number\" step=\"any\">"
           "<input id=\"theading\" placeholder=\"SF heading (deg)\" type=\"number\" step=\"any\">"
           "<div id=\"sectors-box\">"
           "<p style=\"color:#888;font-size:12px;margin:8px 0 4px\">Sector Splits (optional, up to 3)</p>"
           "<div id=\"sector-list\"></div>"
           "<button type=\"button\" onclick=\"addSectorRow()\" "
             "style=\"background:#334;font-size:0.8em;margin-top:4px\">"
             "+ Add Sector Split</button>"
           "</div>"
           "<button onclick=\"addTrack()\">Create Track</button>"
           "</div>"

           // Settings card
           "<div class=\"card\">"
           "<h2>Settings</h2>"
           "<div class=\"row\"><span class=\"label\">SSID</span>"
             "<input id=\"s-ssid\"></div>"
           "<div class=\"row\"><span class=\"label\">Password</span>"
             "<input id=\"s-pass\" type=\"password\"></div>"
           "<div class=\"row\"><span class=\"label\">Brightness</span>"
             "<input id=\"s-bright\" type=\"number\" min=\"0\" max=\"255\"></div>"
           "<div class=\"row\"><span class=\"label\">GPS Rate (Hz)</span>"
             "<input id=\"s-rate\" type=\"number\" min=\"1\" max=\"25\"></div>"
           "<button onclick=\"saveSettings()\">Save Settings</button>"
           "<div id=\"msg\">Saved!</div>"
           "<p style=\"color:#aaa;font-size:12px;margin-top:8px\">"
             "WiFi\xe5\x90\x8d\xe7\xa7\xb0\xe5\x92\x8c\xe5\xaf\x86\xe7\xa0\x81"
             "\xe4\xbf\xae\xe6\x94\xb9\xe5\x90\x8e\xe9\x9c\x80\xe9\x87\x8d\xe5\x90\xaf"
             "\xe7\x94\x9f\xe6\x95\x88\xe3\x80\x82"
             "\xe4\xba\xae\xe5\xba\xa6\xe7\xab\x8b\xe5\x8d\xb3\xe7\x94\x9f\xe6\x95\x88\xe3\x80\x82"
             "GPS\xe9\x87\x87\xe6\xa0\xb7\xe7\x8e\x87\xe4\xbf\xae\xe6\x94\xb9\xe5\x90\x8e"
             "\xe9\x9c\x80\xe9\x87\x8d\xe5\x90\xaf\xe7\x94\x9f\xe6\x95\x88\xe3\x80\x82"
           "</p>"
           "</div>"

           "</body>";
}

static String build_script_section() {
    return "<script>"
           "function $(id){return document.getElementById(id)}"

           // Refresh status every 2s
           "function refreshStatus(){"
             "fetch('/api/status').then(r=>r.json()).then(d=>{"
               "$('gps-fix').textContent=d.gps_fix?'Yes':'No';"
               "$('gps-fix').className='val '+(d.gps_fix?'ok':'err');"
               "$('sats').textContent=d.satellites;"
               "$('rec').textContent=d.recording?'REC':'Idle';"
               "$('rec').className='val '+(d.recording?'warn':'ok');"
               "$('lap').textContent=d.current_lap;"
               "$('best').textContent=d.best_lap_ms>0?"
                 "(d.best_lap_ms/1000).toFixed(3)+'s':'--';"
               "$('track').textContent=d.track||'None';"
               "_isRec=d.recording;updateRecBtn();"
             "}).catch(()=>{})}"

           // Load sessions
           "function loadSessions(){"
             "fetch('/api/sessions').then(r=>r.json()).then(d=>{"
               "let ul=$('sessions');ul.innerHTML='';"
               "if(!d.sessions||!d.sessions.length){"
                 "ul.innerHTML='<li>No sessions</li>';return}"
               "d.sessions.forEach(s=>{"
                 "let li=document.createElement('li');"
                 "li.innerHTML='<a href=\"/files/'+s+'\">'+s+'</a>';"
                 "ul.appendChild(li)})"
             "}).catch(()=>{})}"

           // Load tracks
           "function loadTracks(){"
             "fetch('/api/tracks').then(r=>r.json()).then(d=>{"
               "let ul=$('tracks');ul.innerHTML='';"
               "if(!d.tracks||!d.tracks.length){"
                 "ul.innerHTML='<li>No tracks</li>';return}"
               "d.tracks.forEach(t=>{"
                 "let li=document.createElement('li');"
                 "li.style.display='flex';li.style.justifyContent='space-between';"
                 "li.style.alignItems='center';"
                 "let sp=document.createElement('span');"
                 "sp.textContent=t.name+' ('+t.id+')';"
                 "let bx=document.createElement('span');"
                 "let sb=document.createElement('button');"
                 "sb.textContent='\\u9009\\u4e3a\\u5f53\\u524d';"
                 "sb.style.cssText='padding:4px 8px;margin:0 4px;font-size:0.8em';"
                 "sb.onclick=function(){selectTrack(t.id)};"
                 "let db=document.createElement('button');"
                 "db.textContent='\\u5220\\u9664';"
                 "db.style.cssText='padding:4px 8px;font-size:0.8em;background:#f44';"
                 "db.onclick=function(){deleteTrack(t.id,t.name)};"
                 "bx.appendChild(sb);bx.appendChild(db);"
                 "li.appendChild(sp);li.appendChild(bx);"
                 "ul.appendChild(li)})"
             "}).catch(()=>{})}"

           // Sector row management
           "var _sectorCount=0;"
           "function addSectorRow(){"
             "if(_sectorCount>=3)return;"
             "_sectorCount++;"
             "var n=_sectorCount;"
             "var d=document.createElement('div');"
             "d.id='sec'+n;"
             "d.innerHTML='<p style=\"color:#aaa;font-size:11px\">Split '+n+'</p>'"
               "+'<input id=\"slat1_'+n+'\" placeholder=\"Sector '+n+' lat1\" type=\"number\" step=\"any\">'"
               "+'<input id=\"slon1_'+n+'\" placeholder=\"Sector '+n+' lon1\" type=\"number\" step=\"any\">'"
               "+'<input id=\"slat2_'+n+'\" placeholder=\"Sector '+n+' lat2\" type=\"number\" step=\"any\">'"
               "+'<input id=\"slon2_'+n+'\" placeholder=\"Sector '+n+' lon2\" type=\"number\" step=\"any\">'"
               "+'<input id=\"shd_'+n+'\" placeholder=\"Sector '+n+' heading\" type=\"number\" step=\"any\">';"
             "$('sector-list').appendChild(d)}"

           // Add track
           "function addTrack(){"
             "var secs=[];"
             "for(var i=1;i<=_sectorCount;i++){"
               "var la1=$('slat1_'+i),lo1=$('slon1_'+i);"
               "var la2=$('slat2_'+i),lo2=$('slon2_'+i),hd=$('shd_'+i);"
               "if(la1&&la1.value)secs.push({lat1:+la1.value,lon1:+lo1.value,"
                 "lat2:+la2.value,lon2:+lo2.value,heading:+hd.value})}"
             "let b={name:$('tname').value,"
               "sf_lat1:+$('tlat1').value,sf_lon1:+$('tlon1').value,"
               "sf_lat2:+$('tlat2').value,sf_lon2:+$('tlon2').value,"
               "sf_heading:+$('theading').value,sectors:secs};"
             "fetch('/api/tracks',{method:'POST',"
               "headers:{'Content-Type':'application/json'},"
               "body:JSON.stringify(b)})"
             ".then(r=>r.json()).then(d=>{if(d.ok)loadTracks()})"
             ".catch(()=>{})}"

           // Load settings
           "function loadSettings(){"
             "fetch('/api/settings').then(r=>r.json()).then(d=>{"
               "$('s-ssid').value=d.wifi_ssid;"
               "$('s-pass').value=d.wifi_pass;"
               "$('s-bright').value=d.brightness;"
               "$('s-rate').value=d.gps_rate_hz"
             "}).catch(()=>{})}"

           // Save settings
           "function saveSettings(){"
             "let b={wifi_ssid:$('s-ssid').value,"
               "wifi_pass:$('s-pass').value,"
               "brightness:+$('s-bright').value,"
               "gps_rate_hz:+$('s-rate').value};"
             "fetch('/api/settings',{method:'POST',"
               "headers:{'Content-Type':'application/json'},"
               "body:JSON.stringify(b)})"
             ".then(r=>r.json()).then(d=>{"
               "if(d.ok){let m=$('msg');m.style.display='block';"
                 "setTimeout(()=>m.style.display='none',2000)}"
             "}).catch(()=>{})}"

           // Select track
           "function selectTrack(id){"
             "fetch('/api/tracks/select',{method:'POST',"
               "headers:{'Content-Type':'application/json'},"
               "body:JSON.stringify({id:id})})"
             ".then(r=>r.json()).then(d=>{"
               "if(d.ok){refreshStatus();loadTracks()}"
             "}).catch(()=>{})}"

           // Delete track
           "function deleteTrack(id,name){"
             "if(!confirm('\\u786e\\u8ba4\\u5220\\u9664\\u8d5b\\u9053: '+name+'?'))return;"
             "fetch('/api/tracks/delete',{method:'POST',"
               "headers:{'Content-Type':'application/json'},"
               "body:JSON.stringify({id:id})})"
             ".then(r=>r.json()).then(d=>{"
               "if(d.ok)loadTracks()"
             "}).catch(()=>{})}"

           // Toggle recording
           "var _isRec=false;"
           "function toggleRecording(){"
             "let act=_isRec?'stop':'start';"
             "fetch('/api/recording',{method:'POST',"
               "headers:{'Content-Type':'application/json'},"
               "body:JSON.stringify({action:act})})"
             ".then(r=>r.json()).then(d=>{"
               "if(d.ok){_isRec=d.recording;updateRecBtn()}"
             "}).catch(()=>{})}"
           "function updateRecBtn(){"
             "let b=$('rec-btn');"
             "if(_isRec){b.textContent='\\u505c\\u6b62\\u5f55\\u5236';"
               "b.style.background='#f44'}"
             "else{b.textContent='\\u5f00\\u59cb\\u5f55\\u5236';"
               "b.style.background='#0af'}}"

           // Init
           "refreshStatus();loadSessions();loadTracks();loadSettings();"
           "setInterval(refreshStatus,2000);"

           "</script>";
}
