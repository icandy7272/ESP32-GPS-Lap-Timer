// ============================================================
// /api/settings handlers — GET + POST, local JSON helpers
// ============================================================

#include "wifi_internal.h"
#include "config.h"

#include <Arduino.h>
#include <WebServer.h>

static bool extract_json_string(const char* json, const char* key,
                                char* out, size_t out_size);
static bool extract_json_int(const char* json, const char* key, int* out);
static void apply_settings_from_json(const char* json);

// ============================================================
// GET /api/settings
// ============================================================

void handle_api_settings_get() {
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

void handle_api_settings_post() {
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
