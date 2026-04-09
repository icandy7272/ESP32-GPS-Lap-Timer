#include "track_internal.h"

#include "../track_runtime.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TrackDefinition s_tracks[MAX_TRACKS];
int s_track_count = 0;

static int find_max_track_number() {
    int max_num = 0;
    for (int i = 0; i < s_track_count; i++) {
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
        if (track_parse_track_json(json_buf, &td)) {
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

int track_count() {
    return s_track_count;
}

const TrackDefinition* track_get(int index) {
    if (index < 0 || index >= s_track_count) {
        return nullptr;
    }
    return &s_tracks[index];
}

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

bool track_load_first(TrackDefinition* out) {
    if (!out || s_track_count == 0) {
        return false;
    }
    *out = s_tracks[0];
    return true;
}

bool track_save(const TrackDefinition* track) {
    if (!track || s_track_count >= MAX_TRACKS) {
        return false;
    }

    int next_num = find_max_track_number() + 1;

    TrackDefinition new_track = *track;
    snprintf(new_track.id, sizeof(new_track.id), "track_%03d", next_num);

    char json_buf[JSON_BUF_SIZE];
    if (!track_format_track_json(&new_track, json_buf, sizeof(json_buf))) {
        return false;
    }

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

    s_tracks[s_track_count] = new_track;
    s_track_count++;

    Serial.printf("[track] saved: %s (%s)\n", new_track.id, new_track.name);
    return true;
}

bool track_delete(const char* id) {
    if (!id) {
        return false;
    }

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

    char path[PATH_BUF_LEN];
    snprintf(path, sizeof(path), "%s/%s.json", TRACKS_DIR, id);

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    bool removed = sd.remove(path);
    xSemaphoreGive(spi_mutex);

    if (!track_runtime_should_commit_delete(removed)) {
        Serial.printf("[track] failed to delete file: %s\n", path);
        return false;
    }

    for (int i = idx; i < s_track_count - 1; i++) {
        s_tracks[i] = s_tracks[i + 1];
    }
    s_track_count--;

    Serial.printf("[track] deleted: %s\n", id);
    return true;
}
