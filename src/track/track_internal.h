#pragma once

#include "track.h"

#include "../sdfat_global.h"
#include <freertos/semphr.h>

static constexpr int PATH_BUF_LEN = 128;
static constexpr int JSON_BUF_SIZE = 2048;
static constexpr double AUTO_DETECT_MAX_M = 5000.0;
static constexpr const char* TRACKS_DIR = "tracks";

extern SemaphoreHandle_t spi_mutex;

// Track-store mutex.  Protects all reads/writes against `s_tracks[]`
// and `s_track_count`.  Previously the array was mutated by
// track_save / track_delete on the WiFi task while the main loop
// task read raw pointers through track_get_by_id / track_get, making
// `track select` and `tracks list` racy against phone-UI deletes.
// All public reader APIs that return a copy (track_copy_by_id etc.)
// take this mutex internally; the legacy pointer-returning APIs
// (track_get, track_get_by_id) are kept for boot-time single-task
// callers only.
//
// Lock ordering: session_mutex is OUTSIDE track_store_mutex.  Never
// take session_mutex while holding track_store_mutex.
extern SemaphoreHandle_t track_store_mutex;

extern TrackDefinition s_tracks[MAX_TRACKS];
extern int s_track_count;

bool track_parse_track_json(const char* json, TrackDefinition* out);
bool track_format_track_json(const TrackDefinition* track,
                             char* buf, int buf_len);
