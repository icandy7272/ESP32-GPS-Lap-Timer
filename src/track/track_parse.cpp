#include "track_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_double(const char* json, const char* key, double* out);
static bool parse_float(const char* json, const char* key, float* out);
static bool parse_string(const char* json, const char* key,
                         char* out, int out_len);
static bool parse_detection_line(const char* block, DetectionLine* out);
static int parse_sectors_array(const char* json, DetectionLine* out,
                               int max_sectors);
static const char* find_object_block(const char* json, const char* key,
                                     const char** end);
static const char* find_array_start(const char* json, const char* key);
static const char* find_next_object(const char* pos, const char** end);

bool track_parse_track_json(const char* json, TrackDefinition* out) {
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

static bool parse_double(const char* json, const char* key, double* out) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) {
        return false;
    }

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

static bool parse_float(const char* json, const char* key, float* out) {
    double d;
    if (!parse_double(json, key, &d)) {
        return false;
    }
    *out = (float)d;
    return true;
}

static bool parse_string(const char* json, const char* key,
                         char* out, int out_len) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) {
        return false;
    }

    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) {
        p++;
    }
    if (*p != '"') {
        return false;
    }
    p++;

    int i = 0;
    while (*p && *p != '"' && i < out_len - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';

    return (i > 0);
}

static const char* find_object_block(const char* json, const char* key,
                                     const char** end) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) {
        return nullptr;
    }

    p += strlen(pattern);
    while (*p && *p != '{') {
        p++;
    }
    if (*p != '{') {
        return nullptr;
    }

    const char* start = p;
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
    return p + 1;
}

static const char* find_next_object(const char* pos, const char** end) {
    if (!pos) {
        return nullptr;
    }

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
