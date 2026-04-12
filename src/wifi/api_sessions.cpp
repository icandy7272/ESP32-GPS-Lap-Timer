// ============================================================
// GET /api/sessions — list VBO files on SD
// Enumerates sessions/ directory directly via SdFat.
// No fixed filename buffer or item count limit.
// ============================================================

#include "wifi_internal.h"
#include "../sdfat_global.h"

#include <Arduino.h>
#include <WebServer.h>
#include <ctype.h>
#include <string.h>

static bool has_vbo_extension(const char* name) {
    size_t len = strlen(name);
    if (len < 5) { return false; }
    return (strcasecmp(name + len - 4, ".vbo") == 0);
}

static void session_metadata_defaults(char* date_label, size_t date_label_len,
                                      char* track_name, size_t track_name_len) {
    if (date_label_len > 0) {
        date_label[0] = '\0';
    }
    if (track_name_len > 0) {
        strlcpy(track_name, "Unknown track", track_name_len);
    }
}

static void session_parse_filename_metadata(const char* filename,
                                            char* date_label, size_t date_label_len,
                                            char* track_name, size_t track_name_len) {
    session_metadata_defaults(date_label, date_label_len, track_name, track_name_len);
    if (!filename) {
        return;
    }

    char base[256];
    strlcpy(base, filename, sizeof(base));
    char* ext = strrchr(base, '.');
    if (ext) {
        *ext = '\0';
    }

    size_t len = strlen(base);
    if (len < 18) {
        return;
    }
    for (int i = 0; i < 8; i++) {
        if (!isdigit(static_cast<unsigned char>(base[i]))) {
            return;
        }
    }
    if (base[8] != '_') {
        return;
    }

    char* seq_sep = strrchr(base, '_');
    if (!seq_sep || seq_sep <= base + 9) {
        return;
    }
    char* time_sep = seq_sep - 1;
    while (time_sep > base && *time_sep != '_') {
        time_sep--;
    }
    if (!time_sep || *time_sep != '_') {
        return;
    }

    const char* time_part = time_sep + 1;
    if (strlen(time_part) != 6) {
        return;
    }

    if (date_label_len > 0) {
        snprintf(date_label, date_label_len, "%.4s-%.2s-%.2s %.2s:%.2s",
                 base, base + 4, base + 6, time_part, time_part + 2);
    }

    size_t track_len = static_cast<size_t>(time_sep - (base + 9));
    if (track_len == 0 || track_name_len == 0) {
        return;
    }
    if (track_len >= track_name_len) {
        track_len = track_name_len - 1;
    }
    memcpy(track_name, base + 9, track_len);
    track_name[track_len] = '\0';
}

void handle_api_sessions() {
    if (is_throttled()) {
        server.send(503, "application/json", "{\"error\":\"busy\"}");
        return;
    }

    String json = "{\"sessions\":[";
    bool first = true;

    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        FsFile dir;
        if (dir.open("sessions", O_RDONLY)) {
            FsFile entry;
            char fname[256];
            while (entry.openNext(&dir, O_RDONLY)) {
                if (!entry.isDir()) {
                    entry.getName(fname, sizeof(fname));
                    if (has_vbo_extension(fname)) {
                        char date_label[32];
                        char track_name[64];
                        session_parse_filename_metadata(
                            fname,
                            date_label, sizeof(date_label),
                            track_name, sizeof(track_name));
                        if (!first) { json += ","; }
                        json += "{\"filename\":\"";
                        json += jsonEscapeString(fname);
                        json += "\",\"date\":\"";
                        json += jsonEscapeString(date_label);
                        json += "\",\"track\":\"";
                        json += jsonEscapeString(track_name);
                        json += "\",\"best_lap_ms\":-1}";
                        first = false;
                    }
                }
                entry.close();
            }
            dir.close();
        }
        xSemaphoreGive(spi_mutex);
    }

    json += "]}";
    server.send(200, "application/json", json);
}
