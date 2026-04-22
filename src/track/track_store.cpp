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

// Track-store mutex: guards every read/write of s_tracks[] and
// s_track_count.  Created in setup() before any task that might
// touch the track store is spawned.  See track_internal.h for the
// lock-ordering rules (session_mutex is OUTSIDE this one).
SemaphoreHandle_t track_store_mutex = nullptr;

namespace {

// Take the track-store mutex with a long-but-bounded timeout.
// Returns true if acquired, false on timeout.  We fail closed —
// callers surface an error to the operator rather than silently
// operating on unlocked state.
bool take_track_store_mutex(uint32_t timeout_ms = 1000) {
    if (track_store_mutex == nullptr) {
        // Not yet created (boot-time before setup() ran).  Caller
        // is single-threaded at this point, so unlocked access is
        // safe.  Returning true here mirrors the pre-mutex semantics
        // for init-time code (track_init, track_load_first).
        return true;
    }
    return xSemaphoreTake(track_store_mutex,
                          pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void give_track_store_mutex() {
    if (track_store_mutex != nullptr) {
        xSemaphoreGive(track_store_mutex);
    }
}

}  // namespace

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

// --- Thread-safe copy-out APIs ---
//
// Each takes track_store_mutex, copies the result into the caller's
// buffer, and releases.  Safe against concurrent track_save /
// track_delete mutations on the WiFi task.  Tri-state return so
// callers can distinguish "mutex busy" from "not found".

TrackLookupResult track_copy_by_id(const char* id, TrackDefinition* out) {
    if (!id || !out) {
        return TRACK_LOOKUP_NOT_FOUND;
    }
    if (!take_track_store_mutex()) {
        return TRACK_LOOKUP_BUSY;
    }
    TrackLookupResult r = TRACK_LOOKUP_NOT_FOUND;
    for (int i = 0; i < s_track_count; i++) {
        if (strcmp(s_tracks[i].id, id) == 0) {
            *out = s_tracks[i];
            r = TRACK_LOOKUP_OK;
            break;
        }
    }
    give_track_store_mutex();
    return r;
}

TrackLookupResult track_copy_at(int index, TrackDefinition* out) {
    if (!out) {
        return TRACK_LOOKUP_NOT_FOUND;
    }
    if (!take_track_store_mutex()) {
        return TRACK_LOOKUP_BUSY;
    }
    TrackLookupResult r = TRACK_LOOKUP_NOT_FOUND;
    if (index >= 0 && index < s_track_count) {
        *out = s_tracks[index];
        r = TRACK_LOOKUP_OK;
    }
    give_track_store_mutex();
    return r;
}

int track_snapshot_count() {
    if (!take_track_store_mutex()) {
        // -1 sentinel lets callers distinguish "busy" from "empty".
        // For iteration bounds a negative value is harmless (loop
        // body skipped) and matches "try again later" semantics.
        return -1;
    }
    int n = s_track_count;
    give_track_store_mutex();
    return n;
}

TrackSaveResult track_save_detailed(const TrackDefinition* track,
                                    TrackDefinition* out_saved) {
    if (!track) {
        return TRACK_SAVE_RESULT_INVALID_ARGUMENT;
    }

    // Hold track_store_mutex across the ENTIRE save.  Codex review
    // 2026-04-22 round 4 caught an ID-reservation race: the previous
    // version released the mutex between id generation and the SD
    // write, so two concurrent saves could compute the same
    // track_NNN and end up writing to the same file + appending
    // duplicate in-memory entries.
    //
    // Holding across the SD I/O means readers (track_copy_by_id,
    // `tracks list`, etc.) stall for 100-500 ms during a save.
    // Saves are rare (operator creates tracks occasionally, not per
    // fix) and the alternative — append-then-rollback — is more
    // code for a narrow win.  Live_map's bootstrap tolerates this
    // cleanly via the 1000 ms mutex timeout on reads.
    if (!take_track_store_mutex(3000)) {
        Serial.println("[track] save: store mutex contended");
        return TRACK_SAVE_RESULT_STORE_BUSY;
    }
    if (s_track_count >= MAX_TRACKS) {
        give_track_store_mutex();
        return TRACK_SAVE_RESULT_CAPACITY_REACHED;
    }
    int next_num = find_max_track_number() + 1;

    TrackDefinition new_track = *track;
    snprintf(new_track.id, sizeof(new_track.id), "track_%03d", next_num);

    char json_buf[JSON_BUF_SIZE];
    if (!track_format_track_json(&new_track, json_buf, sizeof(json_buf))) {
        give_track_store_mutex();
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
        give_track_store_mutex();
        Serial.printf("[track] failed to create directory: %s\n", TRACKS_DIR);
        return TRACK_SAVE_RESULT_DIRECTORY_CREATE_FAILED;
    }

    FsFile file;
    if (!file.open(path, O_WRONLY | O_CREAT | O_TRUNC)) {
        xSemaphoreGive(spi_mutex);
        give_track_store_mutex();
        Serial.printf("[track] failed to open: %s\n", path);
        return TRACK_SAVE_RESULT_FILE_OPEN_FAILED;
    }

    size_t len = strlen(json_buf);
    size_t written = file.write(json_buf, len);
    file.flush();
    if (written != len) {
        file.close();
        xSemaphoreGive(spi_mutex);
        give_track_store_mutex();
        Serial.printf("[track] partial write: %u/%u bytes to %s\n",
                      (unsigned)written, (unsigned)len, path);
        return TRACK_SAVE_RESULT_FILE_WRITE_SHORT;
    }
    if (!file.sync()) {
        file.close();
        xSemaphoreGive(spi_mutex);
        give_track_store_mutex();
        Serial.printf("[track] sync failed: %s\n", path);
        return TRACK_SAVE_RESULT_FILE_SYNC_FAILED;
    }
    file.close();

    // Persist the new directory entry before claiming success.  Session
    // finalization already syncs its parent directory; track creation
    // needs the same durability so a just-saved draft is actually
    // visible on the SD card after the UI reports success.
    bool dir_synced = false;
    FsFile dir;
    if (dir.open(TRACKS_DIR, O_RDONLY)) {
        dir_synced = dir.sync();
        dir.close();
    }
    bool exists_after_write = sd.exists(path);

    xSemaphoreGive(spi_mutex);

    if (!dir_synced) {
        give_track_store_mutex();
        Serial.printf("[track] directory sync failed: %s\n", TRACKS_DIR);
        return TRACK_SAVE_RESULT_FILE_SYNC_FAILED;
    }
    if (!exists_after_write) {
        give_track_store_mutex();
        Serial.printf("[track] file missing after save: %s\n", path);
        return TRACK_SAVE_RESULT_FILE_SYNC_FAILED;
    }

    // Final array mutation happens inside the SAME critical section
    // that reserved next_num — no release window for another save to
    // claim the same id.  The s_track_count bound check (repeated)
    // is defensive but not strictly needed since we hold the mutex.
    s_tracks[s_track_count] = new_track;
    s_track_count++;
    if (out_saved != nullptr) {
        *out_saved = new_track;
    }
    give_track_store_mutex();

    Serial.printf("[track] saved: %s (%s)\n", new_track.id, new_track.name);
    return dir_recreated ? TRACK_SAVE_RESULT_SUCCESS_DIR_RECREATED
                         : TRACK_SAVE_RESULT_SUCCESS;
}

bool track_save(const TrackDefinition* track) {
    return track_creation_save_result_succeeded(
        track_save_detailed(track, /*out_saved=*/nullptr));
}

bool track_delete(const char* id) {
    if (!id) {
        return false;
    }

    // Hold track_store_mutex across the ENTIRE delete: lookup, SD
    // remove, and in-memory shift.  Codex review 2026-04-22 round 5
    // caught a disk/memory split where the old two-phase version
    // dropped the mutex between SD remove and the shift; if a
    // concurrent save held the mutex longer than the 2000 ms shift-
    // timeout (now more likely since save holds across full I/O),
    // delete would leave the file gone from SD but the entry still
    // in s_tracks[] until reboot.  Matching the save path's
    // all-in-one pattern keeps disk + memory consistent.
    //
    // Lock order: track_store_mutex → spi_mutex, same as save.
    if (!take_track_store_mutex(3000)) {
        Serial.println("[track] delete: store mutex contended");
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
        give_track_store_mutex();
        return false;
    }

    char path[PATH_BUF_LEN];
    snprintf(path, sizeof(path), "%s/%s.json", TRACKS_DIR, id);

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    bool removed = sd.remove(path);
    xSemaphoreGive(spi_mutex);

    if (!track_runtime_should_commit_delete(removed)) {
        give_track_store_mutex();
        Serial.printf("[track] failed to delete file: %s\n", path);
        return false;
    }

    // Still inside the critical section we started with; `idx` is
    // still valid because nothing else could have mutated
    // s_tracks[] while we held the mutex.
    for (int i = idx; i < s_track_count - 1; i++) {
        s_tracks[i] = s_tracks[i + 1];
    }
    s_track_count--;
    give_track_store_mutex();

    Serial.printf("[track] deleted: %s\n", id);
    return true;
}
