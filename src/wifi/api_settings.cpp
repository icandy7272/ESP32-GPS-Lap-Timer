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
static bool apply_settings_from_json(const char* json, const char** err_out);

// Input-validation helper.  settings.json is written with a hand-rolled
// JSON formatter (no escaping), and the matching reader stops at the
// first `"` it finds.  If the user POSTs a SSID or password containing
// `"` or `\`, the saved file becomes non-JSON and the next boot either
// truncates the field or rejects it.  Rather than introduce a full
// escaper/unescaper pair, we reject inputs that would break the parser.
// Printable ASCII only, no quote, no backslash — good enough for the
// WPA2 ASCII SSID/passphrase surface, and prevents any bad byte from
// reaching config_save().
static bool is_safe_wifi_string(const char* s) {
    for (int i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c > 0x7E) return false;
        if (c == '"' || c == '\\') return false;
    }
    return true;
}

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

    const char* err = nullptr;
    if (!apply_settings_from_json(json, &err)) {
        char buf[96];
        snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}",
                 err ? err : "invalid input");
        server.send(400, "application/json", buf);
        return;
    }

    // Apply brightness immediately (LEDC channel 0 = TFT backlight)
    ledcWrite(0, app_config.brightness);

    bool ok = config_save();
    if (ok) {
        server.send(200, "application/json", "{\"ok\":true}");
    } else {
        server.send(500, "application/json", "{\"error\":\"save failed\"}");
    }
}

// Returns false (and sets *err_out to a short literal) if any field is
// present but rejected, in which case nothing is applied to app_config.
// Missing keys are OK — they keep the prior value.
static bool apply_settings_from_json(const char* json, const char** err_out) {
    // Validate all string fields first; apply only after every field passes.
    char new_ssid[sizeof(app_config.wifi_ssid)] = {0};
    char new_pass[sizeof(app_config.wifi_pass)] = {0};
    bool have_ssid = extract_json_string(json, "wifi_ssid",
                                         new_ssid, sizeof(new_ssid));
    bool have_pass = extract_json_string(json, "wifi_pass",
                                         new_pass, sizeof(new_pass));

    if (have_ssid) {
        size_t len = strlen(new_ssid);
        if (len == 0 || len > sizeof(app_config.wifi_ssid) - 1) {
            *err_out = "wifi_ssid length out of range";
            return false;
        }
        if (!is_safe_wifi_string(new_ssid)) {
            *err_out = "wifi_ssid contains disallowed characters";
            return false;
        }
    }
    if (have_pass) {
        size_t len = strlen(new_pass);
        if (len > sizeof(app_config.wifi_pass) - 1) {
            *err_out = "wifi_pass too long";
            return false;
        }
        // Allow empty (open network); WPA2 requires 8-63 but that is a
        // radio-layer policy, not a parser-safety concern.
        if (len > 0 && !is_safe_wifi_string(new_pass)) {
            *err_out = "wifi_pass contains disallowed characters";
            return false;
        }
    }

    int brightness_val = 0;
    int rate_val = 0;
    bool have_brightness = extract_json_int(json, "brightness", &brightness_val);
    bool have_rate = extract_json_int(json, "gps_rate_hz", &rate_val);

    if (have_brightness && (brightness_val < 0 || brightness_val > 255)) {
        *err_out = "brightness out of range";
        return false;
    }
    if (have_rate && (rate_val < 1 || rate_val > 25)) {
        *err_out = "gps_rate_hz out of range (1-25)";
        return false;
    }

    // All validation passed — commit atomically.
    if (have_ssid) {
        strlcpy(app_config.wifi_ssid, new_ssid, sizeof(app_config.wifi_ssid));
    }
    if (have_pass) {
        strlcpy(app_config.wifi_pass, new_pass, sizeof(app_config.wifi_pass));
    }
    if (have_brightness) {
        app_config.brightness = (uint8_t)brightness_val;
    }
    if (have_rate) {
        app_config.gps_rate_hz = (uint8_t)rate_val;
    }
    return true;
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
