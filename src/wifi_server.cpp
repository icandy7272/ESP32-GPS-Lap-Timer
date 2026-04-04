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
    server.on("/api/tracks",     HTTP_GET,  handle_api_tracks);
    server.on("/api/tracks",     HTTP_POST, handle_api_tracks_post);
    server.on("/api/settings",   HTTP_GET,  handle_api_settings_get);
    server.on("/api/settings",   HTTP_POST, handle_api_settings_post);
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

    // Determine next track ID by counting existing files
    int next_id = find_next_track_id();

    char filename[48];
    snprintf(filename, sizeof(filename), "/tracks/track_%03d.json", next_id);

    bool ok = write_track_file(filename, body);
    if (ok) {
        char resp[96];
        snprintf(resp, sizeof(resp),
                 "{\"ok\":true,\"id\":\"track_%03d\"}", next_id);
        server.send(201, "application/json", resp);
    } else {
        server.send(500, "application/json", "{\"error\":\"write failed\"}");
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
           "<input id=\"tlat1\" placeholder=\"SF lat1\" type=\"number\" step=\"any\">"
           "<input id=\"tlon1\" placeholder=\"SF lon1\" type=\"number\" step=\"any\">"
           "<input id=\"tlat2\" placeholder=\"SF lat2\" type=\"number\" step=\"any\">"
           "<input id=\"tlon2\" placeholder=\"SF lon2\" type=\"number\" step=\"any\">"
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
                 "li.textContent=t.name+' ('+t.id+')';"
                 "ul.appendChild(li)})"
             "}).catch(()=>{})}"

           // Add track
           "function addTrack(){"
             "let b={name:$('tname').value,"
               "start_finish:{lat1:+$('tlat1').value,lon1:+$('tlon1').value,"
                 "lat2:+$('tlat2').value,lon2:+$('tlon2').value}};"
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

           // Init
           "refreshStatus();loadSessions();loadTracks();loadSettings();"
           "setInterval(refreshStatus,2000);"

           "</script>";
}
