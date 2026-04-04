// ============================================================
// Runtime configuration — ESP32-S3 GPS Lap Timer
// Hand-parsed JSON to avoid ArduinoJson dependency.
// Format: {"wifi_ssid":"...","wifi_pass":"...","brightness":N,"gps_rate_hz":N}
//
// Requires: SD library (included with Arduino ESP32 core)
// spi_mutex must be created before calling config_load/save.
// ============================================================

#include "config.h"

#include <Arduino.h>
#include <SD.h>

// --- Extern references (created in main.cpp) ---
extern SemaphoreHandle_t spi_mutex;

// --- Global config instance ---
AppConfig app_config;

// --- Constants ---
static const char* CONFIG_DIR  = "/config";
static const char* CONFIG_PATH = "/config/settings.json";

static const char* DEFAULT_SSID = "GPS-LapTimer";
static const char* DEFAULT_PASS = "12345678";
static const uint8_t DEFAULT_BRIGHTNESS  = 200;
static const uint8_t DEFAULT_GPS_RATE_HZ = 25;

// ============================================================
// Defaults
// ============================================================

void config_set_defaults() {
    memset(&app_config, 0, sizeof(app_config));
    strlcpy(app_config.wifi_ssid, DEFAULT_SSID, sizeof(app_config.wifi_ssid));
    strlcpy(app_config.wifi_pass, DEFAULT_PASS, sizeof(app_config.wifi_pass));
    app_config.brightness  = DEFAULT_BRIGHTNESS;
    app_config.gps_rate_hz = DEFAULT_GPS_RATE_HZ;
}

// ============================================================
// JSON helpers — simple key extraction (no external library)
// ============================================================

// Extract a quoted string value for "key":"value".
// Returns true if found and copied into out_buf.
static bool json_extract_string(const char* json,
                                const char* key,
                                char* out_buf,
                                size_t buf_size) {
    // Build search pattern: "key":"
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);

    const char* start = strstr(json, pattern);
    if (!start) {
        return false;
    }
    start += strlen(pattern);

    const char* end = strchr(start, '"');
    if (!end) {
        return false;
    }

    size_t len = (size_t)(end - start);
    if (len >= buf_size) {
        len = buf_size - 1;
    }
    memcpy(out_buf, start, len);
    out_buf[len] = '\0';
    return true;
}

// Extract an integer value for "key":123.
// Returns true if found.
static bool json_extract_int(const char* json,
                             const char* key,
                             int* out_value) {
    // Build search pattern: "key":
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    const char* start = strstr(json, pattern);
    if (!start) {
        return false;
    }
    start += strlen(pattern);

    // Skip whitespace
    while (*start == ' ' || *start == '\t') {
        start++;
    }

    char* end_ptr = nullptr;
    long val = strtol(start, &end_ptr, 10);
    if (end_ptr == start) {
        return false;
    }

    *out_value = (int)val;
    return true;
}

// ============================================================
// Load from SD
// ============================================================

bool config_load() {
    config_set_defaults();

    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("[config] Failed to acquire spi_mutex for load");
        return false;
    }

    File f = SD.open(CONFIG_PATH, FILE_READ);
    if (!f) {
        xSemaphoreGive(spi_mutex);
        Serial.println("[config] No settings.json found, using defaults");
        return false;
    }

    // Read entire file (expected < 256 bytes)
    char buf[256];
    size_t bytes_read = f.readBytes(buf, sizeof(buf) - 1);
    buf[bytes_read] = '\0';
    f.close();

    xSemaphoreGive(spi_mutex);

    // Parse fields — keep defaults for any missing key
    json_extract_string(buf, "wifi_ssid",
                        app_config.wifi_ssid,
                        sizeof(app_config.wifi_ssid));

    json_extract_string(buf, "wifi_pass",
                        app_config.wifi_pass,
                        sizeof(app_config.wifi_pass));

    int tmp = 0;
    if (json_extract_int(buf, "brightness", &tmp)) {
        app_config.brightness = (uint8_t)constrain(tmp, 0, 255);
    }
    if (json_extract_int(buf, "gps_rate_hz", &tmp)) {
        app_config.gps_rate_hz = (uint8_t)constrain(tmp, 1, 25);
    }

    Serial.printf("[config] Loaded: ssid=%s brightness=%u gps_rate=%u\n",
                  app_config.wifi_ssid,
                  app_config.brightness,
                  app_config.gps_rate_hz);
    return true;
}

// ============================================================
// Save to SD
// ============================================================

bool config_save() {
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("[config] Failed to acquire spi_mutex for save");
        return false;
    }

    // Ensure directory exists
    if (!SD.exists(CONFIG_DIR)) {
        SD.mkdir(CONFIG_DIR);
    }

    File f = SD.open(CONFIG_PATH, FILE_WRITE);
    if (!f) {
        xSemaphoreGive(spi_mutex);
        Serial.println("[config] Failed to open settings.json for write");
        return false;
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

    size_t written = f.print(buf);
    f.flush();
    f.close();

    xSemaphoreGive(spi_mutex);

    if (written == 0) {
        Serial.println("[config] Write failed (0 bytes)");
        return false;
    }

    Serial.println("[config] Settings saved to SD");
    return true;
}
