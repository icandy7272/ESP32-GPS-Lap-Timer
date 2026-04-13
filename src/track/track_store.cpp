#include "track_internal.h"

#include "../track_runtime.h"
#include "../track_creation_feedback.h"

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

TrackSaveResult track_save_detailed(const TrackDefinition* track) {
    if (!track) {
        return TRACK_SAVE_RESULT_INVALID_ARGUMENT;
    }
    if (s_track_count >= MAX_TRACKS) {
        return TRACK_SAVE_RESULT_CAPACITY_REACHED;
    }

    int next_num = find_max_track_number() + 1;

    TrackDefinition new_track = *track;
    snprintf(new_track.id, sizeof(new_track.id), "track_%03d", next_num);

    char json_buf[JSON_BUF_SIZE];
    if (!track_format_track_json(&new_track, json_buf, sizeof(json_buf))) {
        return TRACK_SAVE_RESULT_FORMAT_FAILED;
    }

    char path[PATH_BUF_LEN];
    snprintf(path, sizeof(path), "%s/track_%03d.json", TRACKS_DIR, next_num);

    xSemaphoreTake(spi_mutex, portMAX_DELAY);

    bool dir_ok = sd.exists(TRACKS_DIR);
    bool dir_recreated = false;
    if (!dir_ok) {
        Serial.printf("[track] directory missing: %s — recreating\n", TRACKS_DIR);
        dir_ok = sd.mkdir(TRACKS_DIR);
        dir_recreated = dir_ok;
    }
    if (!dir_ok) {
        xSemaphoreGive(spi_mutex);
        Serial.printf("[track] failed to create directory: %s\n", TRACKS_DIR);
        return TRACK_SAVE_RESULT_DIRECTORY_CREATE_FAILED;
    }

    FsFile file;
    if (!file.open(path, O_WRONLY | O_CREAT | O_TRUNC)) {
        xSemaphoreGive(spi_mutex);
        Serial.printf("[track] failed to open: %s\n", path);
        return TRACK_SAVE_RESULT_FILE_OPEN_FAILED;
    }

    size_t len = strlen(json_buf);
    size_t written = file.write(json_buf, len);
    file.flush();
    if (written != len) {
        file.close();
        xSemaphoreGive(spi_mutex);
        Serial.printf("[track] partial write: %u/%u bytes to %s\n",
                      (unsigned)written, (unsigned)len, path);
        return TRACK_SAVE_RESULT_FILE_WRITE_SHORT;
    }
    if (!file.sync()) {
        file.close();
        xSemaphoreGive(spi_mutex);
        Serial.printf("[track] sync failed: %s\n", path);
        return TRACK_SAVE_RESULT_FILE_SYNC_FAILED;
    }
    file.close();

    xSemaphoreGive(spi_mutex);

    s_tracks[s_track_count] = new_track;
    s_track_count++;

    Serial.printf("[track] saved: %s (%s)\n", new_track.id, new_track.name);
    return dir_recreated ? TRACK_SAVE_RESULT_SUCCESS_DIR_RECREATED
                         : TRACK_SAVE_RESULT_SUCCESS;
}

bool track_save(const TrackDefinition* track) {
    return track_creation_save_result_succeeded(track_save_detailed(track));
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
