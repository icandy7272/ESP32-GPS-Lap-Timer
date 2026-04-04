// ============================================================
// Track Management Module — ESP32-S3 GPS Lap Timer
//
// Loads/saves track definitions as JSON on the SD card.
// Hand-rolled JSON parser (no ArduinoJson dependency).
// Uses SdFat on shared SPI bus protected by spi_mutex.
// See docs/ARCHITECTURE.md sections 1 and 8.
// ============================================================

#include "track.h"
#include "types.h"
#include "pins.h"
#include "lap_timer.h"  // haversine_m()

#include <Arduino.h>
#include <SdFat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

// ---- External shared resources (created in main.cpp) --------

extern SemaphoreHandle_t spi_mutex;
extern SdFat             sd;

// ---- Constants ----------------------------------------------

static constexpr int    PATH_BUF_LEN      = 128;
static constexpr int    JSON_BUF_SIZE      = 2048;
static constexpr double AUTO_DETECT_MAX_M  = 5000.0;
static const char*      TRACKS_DIR         = "tracks";

// ---- Module state -------------------------------------------

static TrackDefinition s_tracks[MAX_TRACKS];
static int             s_track_count = 0;

// ---- Forward declarations -----------------------------------

static bool parse_track_json(const char* json, TrackDefinition* out);
static bool parse_double(const char* json, const char* key, double* out);
static bool parse_float(const char* json, const char* key, float* out);
static bool parse_string(const char* json, const char* key,
                         char* out, int out_len);
static bool parse_detection_line(const char* block, DetectionLine* out);
static int  parse_sectors_array(const char* json, DetectionLine* out,
                                int max_sectors);
static const char* find_object_block(const char* json, const char* key,
                                     const char** end);
static const char* find_array_start(const char* json, const char* key);
static const char* find_next_object(const char* pos, const char** end);
static int  find_max_track_number();
static bool format_track_json(const TrackDefinition* track,
                              char* buf, int buf_len);

// ============================================================
// Public API
// ============================================================

bool track_init() {
    s_track_count = 0;

    xSemaphoreTake(spi_mutex, portMAX_DELAY);

    FsFile dir;
    if (!dir.open(TRACKS_DIR, O_RDONLY)) {
        xSemaphoreGive(spi_mutex);
        Serial.println("[track] tracks/ directory not found");
        return true;  // no tracks is not an error
    }

    FsFile entry;
    char fname[64];
    char json_buf[JSON_BUF_SIZE];

    while (s_track_count < MAX_TRACKS && entry.openNext(&dir, O_RDONLY)) {
        if (entry.isDir()) {
            entry.close();
            continue;
        }

        entry.getName(fname, sizeof(fname));
        if (strncmp(fname, "track_", 6) != 0 || !strstr(fname, ".json")) {
            entry.close();
            continue;
        }

        int bytes_read = entry.read(json_buf, sizeof(json_buf) - 1);
        entry.close();

        if (bytes_read <= 0) {
            continue;
        }
        json_buf[bytes_read] = '\0';

        TrackDefinition td = {};
        if (parse_track_json(json_buf, &td)) {
            s_tracks[s_track_count] = td;
            s_track_count++;
            Serial.printf("[track] loaded: %s (%s)\n", td.id, td.name);
        } else {
            Serial.printf("[track] failed to parse: %s\n", fname);
        }
    }

    dir.close();
    xSemaphoreGive(spi_mutex);

    Serial.printf("[track] %d track(s) loaded\n", s_track_count);
    return true;
}

// ------------------------------------------------------------

int track_count() {
    return s_track_count;
}

// ------------------------------------------------------------

const TrackDefinition* track_get(int index) {
    if (index < 0 || index >= s_track_count) {
        return nullptr;
    }
    return &s_tracks[index];
}

// ------------------------------------------------------------

const TrackDefinition* track_get_by_id(const char* id) {
    if (!id) {
        return nullptr;
    }
    for (int i = 0; i < s_track_count; i++) {
        if (strcmp(s_tracks[i].id, id) == 0) {
            return &s_tracks[i];
        }
    }
    return nullptr;
}

// ------------------------------------------------------------

const TrackDefinition* track_auto_detect(double lat, double lon) {
    const TrackDefinition* best = nullptr;
    double best_dist = AUTO_DETECT_MAX_M;

    for (int i = 0; i < s_track_count; i++) {
        double dist = haversine_m(lat, lon,
                                  s_tracks[i].center_lat_deg,
                                  s_tracks[i].center_lon_deg);
        if (dist < best_dist) {
            best_dist = dist;
            best = &s_tracks[i];
        }
    }
    return best;
}

// ------------------------------------------------------------

bool track_load_first(TrackDefinition* out) {
    if (!out || s_track_count == 0) {
        return false;
    }
    *out = s_tracks[0];
    return true;
}

// ------------------------------------------------------------

bool track_save(const TrackDefinition* track) {
    if (!track || s_track_count >= MAX_TRACKS) {
        return false;
    }

    // Generate ID from max existing number + 1
    int next_num = find_max_track_number() + 1;

    TrackDefinition new_track = *track;
    snprintf(new_track.id, sizeof(new_track.id), "track_%03d", next_num);

    // Format JSON
    char json_buf[JSON_BUF_SIZE];
    if (!format_track_json(&new_track, json_buf, sizeof(json_buf))) {
        return false;
    }

    // Write to SD
    char path[PATH_BUF_LEN];
    snprintf(path, sizeof(path), "%s/track_%03d.json", TRACKS_DIR, next_num);

    xSemaphoreTake(spi_mutex, portMAX_DELAY);

    FsFile file;
    bool ok = file.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (ok) {
        file.write(json_buf, strlen(json_buf));
        file.flush();
        file.sync();
        file.close();
    }

    xSemaphoreGive(spi_mutex);

    if (!ok) {
        Serial.printf("[track] failed to write: %s\n", path);
        return false;
    }

    // Add to in-memory array
    s_tracks[s_track_count] = new_track;
    s_track_count++;

    Serial.printf("[track] saved: %s (%s)\n", new_track.id, new_track.name);
    return true;
}

// ------------------------------------------------------------

bool track_delete(const char* id) {
    if (!id) {
        return false;
    }

    // Find in memory
    int idx = -1;
    for (int i = 0; i < s_track_count; i++) {
        if (strcmp(s_tracks[i].id, id) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return false;
    }

    // Delete file from SD
    char path[PATH_BUF_LEN];
    snprintf(path, sizeof(path), "%s/%s.json", TRACKS_DIR, id);

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    bool removed = sd.remove(path);
    xSemaphoreGive(spi_mutex);

    if (!removed) {
        Serial.printf("[track] failed to delete file: %s\n", path);
        // Continue to remove from memory anyway
    }

    // Shift remaining entries
    for (int i = idx; i < s_track_count - 1; i++) {
        s_tracks[i] = s_tracks[i + 1];
    }
    s_track_count--;

    Serial.printf("[track] deleted: %s\n", id);
    return true;
}

// ============================================================
// JSON Parsing Helpers
// ============================================================

static bool parse_track_json(const char* json, TrackDefinition* out) {
    if (!json || !out) {
        return false;
    }

    memset(out, 0, sizeof(TrackDefinition));

    if (!parse_string(json, "id", out->id, sizeof(out->id))) {
        return false;
    }
    if (!parse_string(json, "name", out->name, sizeof(out->name))) {
        return false;
    }
    if (!parse_double(json, "center_lat", &out->center_lat_deg)) {
        return false;
    }
    if (!parse_double(json, "center_lon", &out->center_lon_deg)) {
        return false;
    }

    // approx_length_m is optional
    float len = 0;
    if (parse_float(json, "approx_length_m", &len)) {
        out->approx_length_m = len;
    }

    // Parse start_finish block
    const char* sf_end = nullptr;
    const char* sf_block = find_object_block(json, "start_finish", &sf_end);
    if (!sf_block) {
        return false;
    }
    if (!parse_detection_line(sf_block, &out->start_finish)) {
        return false;
    }

    // Parse sectors array (optional, 0 sectors is valid)
    int sector_line_count = parse_sectors_array(
        json, out->sectors, MAX_SECTORS - 1);
    // sector_count = split lines + 1 (the start/finish is always sector 1)
    out->sector_count = sector_line_count + 1;

    return true;
}

// ------------------------------------------------------------

static bool parse_detection_line(const char* block, DetectionLine* out) {
    if (!block || !out) {
        return false;
    }

    double lat1, lon1, lat2, lon2;
    float heading;

    if (!parse_double(block, "lat1", &lat1)) return false;
    if (!parse_double(block, "lon1", &lon1)) return false;
    if (!parse_double(block, "lat2", &lat2)) return false;
    if (!parse_double(block, "lon2", &lon2)) return false;
    if (!parse_float(block, "heading", &heading)) return false;

    out->lat1_deg = lat1;
    out->lon1_deg = lon1;
    out->lat2_deg = lat2;
    out->lon2_deg = lon2;
    out->valid_heading_deg = heading;

    return true;
}

// ------------------------------------------------------------

static int parse_sectors_array(const char* json, DetectionLine* out,
                               int max_sectors) {
    const char* arr_start = find_array_start(json, "sectors");
    if (!arr_start) {
        return 0;
    }

    int count = 0;
    const char* pos = arr_start;

    while (count < max_sectors) {
        const char* obj_end = nullptr;
        const char* obj = find_next_object(pos, &obj_end);
        if (!obj) {
            break;
        }

        // Extract the block between { and }
        int block_len = (int)(obj_end - obj);
        if (block_len <= 0 || block_len >= JSON_BUF_SIZE) {
            break;
        }

        if (parse_detection_line(obj, &out[count])) {
            count++;
        }

        pos = obj_end + 1;
    }

    return count;
}

// ============================================================
// Low-level JSON value extraction
// ============================================================

static bool parse_double(const char* json, const char* key, double* out) {
    // Search for "key": or "key" :
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) {
        return false;
    }

    // Skip past key and find colon
    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) {
        p++;
    }
    if (!*p) {
        return false;
    }

    char* end = nullptr;
    *out = strtod(p, &end);
    return (end != p);
}

// ------------------------------------------------------------

static bool parse_float(const char* json, const char* key, float* out) {
    double d;
    if (!parse_double(json, key, &d)) {
        return false;
    }
    *out = (float)d;
    return true;
}

// ------------------------------------------------------------

static bool parse_string(const char* json, const char* key,
                         char* out, int out_len) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) {
        return false;
    }

    p += strlen(pattern);
    // Skip whitespace and colon
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) {
        p++;
    }
    // Expect opening quote
    if (*p != '"') {
        return false;
    }
    p++;

    // Copy until closing quote
    int i = 0;
    while (*p && *p != '"' && i < out_len - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';

    return (i > 0);
}

// ============================================================
// JSON structure navigation
// ============================================================

static const char* find_object_block(const char* json, const char* key,
                                     const char** end) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) {
        return nullptr;
    }

    // Find opening brace
    p += strlen(pattern);
    while (*p && *p != '{') {
        p++;
    }
    if (*p != '{') {
        return nullptr;
    }

    const char* start = p;

    // Find matching closing brace (handle nesting)
    int depth = 0;
    while (*p) {
        if (*p == '{') depth++;
        if (*p == '}') {
            depth--;
            if (depth == 0) {
                if (end) *end = p;
                return start;
            }
        }
        p++;
    }
    return nullptr;
}

// ------------------------------------------------------------

static const char* find_array_start(const char* json, const char* key) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) {
        return nullptr;
    }

    p += strlen(pattern);
    while (*p && *p != '[') {
        p++;
    }
    if (*p != '[') {
        return nullptr;
    }
    return p + 1;  // past the '['
}

// ------------------------------------------------------------

static const char* find_next_object(const char* pos, const char** end) {
    if (!pos) {
        return nullptr;
    }

    // Find next '{'
    while (*pos && *pos != '{' && *pos != ']') {
        pos++;
    }
    if (*pos != '{') {
        return nullptr;
    }

    const char* start = pos;
    int depth = 0;

    while (*pos) {
        if (*pos == '{') depth++;
        if (*pos == '}') {
            depth--;
            if (depth == 0) {
                if (end) *end = pos;
                return start;
            }
        }
        pos++;
    }
    return nullptr;
}

// ============================================================
// Track numbering and JSON formatting
// ============================================================

static int find_max_track_number() {
    int max_num = 0;
    for (int i = 0; i < s_track_count; i++) {
        // Parse number from "track_NNN"
        const char* p = s_tracks[i].id;
        if (strncmp(p, "track_", 6) == 0) {
            int num = atoi(p + 6);
            if (num > max_num) {
                max_num = num;
            }
        }
    }
    return max_num;
}

// ------------------------------------------------------------

static bool format_track_json(const TrackDefinition* track,
                              char* buf, int buf_len) {
    int pos = 0;

    pos += snprintf(buf + pos, buf_len - pos,
        "{\n"
        "  \"id\": \"%s\",\n"
        "  \"name\": \"%s\",\n"
        "  \"center_lat\": %.7f,\n"
        "  \"center_lon\": %.7f,\n"
        "  \"approx_length_m\": %.0f,\n",
        track->id,
        track->name,
        track->center_lat_deg,
        track->center_lon_deg,
        (double)track->approx_length_m);

    // start_finish
    pos += snprintf(buf + pos, buf_len - pos,
        "  \"start_finish\": {\n"
        "    \"lat1\": %.7f,\n"
        "    \"lon1\": %.7f,\n"
        "    \"lat2\": %.7f,\n"
        "    \"lon2\": %.7f,\n"
        "    \"heading\": %.1f\n"
        "  }",
        track->start_finish.lat1_deg,
        track->start_finish.lon1_deg,
        track->start_finish.lat2_deg,
        track->start_finish.lon2_deg,
        (double)track->start_finish.valid_heading_deg);

    // sectors
    int sector_lines = track->sector_count - 1;
    if (sector_lines > 0) {
        pos += snprintf(buf + pos, buf_len - pos, ",\n  \"sectors\": [\n");

        for (int i = 0; i < sector_lines && i < MAX_SECTORS - 1; i++) {
            if (i > 0) {
                pos += snprintf(buf + pos, buf_len - pos, ",\n");
            }
            const DetectionLine* s = &track->sectors[i];
            pos += snprintf(buf + pos, buf_len - pos,
                "    {\n"
                "      \"lat1\": %.7f,\n"
                "      \"lon1\": %.7f,\n"
                "      \"lat2\": %.7f,\n"
                "      \"lon2\": %.7f,\n"
                "      \"heading\": %.1f\n"
                "    }",
                s->lat1_deg, s->lon1_deg,
                s->lat2_deg, s->lon2_deg,
                (double)s->valid_heading_deg);
        }

        pos += snprintf(buf + pos, buf_len - pos, "\n  ]");
    } else {
        pos += snprintf(buf + pos, buf_len - pos, ",\n  \"sectors\": []");
    }

    pos += snprintf(buf + pos, buf_len - pos, "\n}\n");

    return (pos < buf_len);
}
