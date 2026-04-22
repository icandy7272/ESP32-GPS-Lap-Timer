#pragma once

// ============================================================
// Track Management Module — ESP32-S3 GPS Lap Timer
//
// Loads track definitions from SD:/tracks/track_NNN.json,
// provides auto-detection by GPS proximity, and supports
// save/delete operations via the wifi_server API.
//
// Uses SdFat on shared SPI bus protected by spi_mutex.
// See docs/ARCHITECTURE.md sections 1 and 8.
// ============================================================

#include <stdbool.h>
#include "types.h"
#include "track_creation_feedback.h"

// Maximum number of tracks held in memory.
static constexpr int MAX_TRACKS = 20;
static constexpr int MAX_NEARBY_TRACKS = 3;

typedef struct {
    const TrackDefinition* track;
    double distance_m;
} NearbyTrackCandidate;

// --- Initialization ---
// Scan SD:/tracks/ for track_*.json files, parse each into
// a TrackDefinition, and store in a static array.
// Returns true if SD access succeeded (0 tracks is still OK).
bool track_init();

// --- Queries ---
//
// LEGACY POINTER-RETURNING APIS:
// The functions below (track_count, track_get, track_get_by_id,
// track_auto_detect, track_find_nearby) return raw pointers into the
// global s_tracks[] array.  They are NOT safe against concurrent
// track_save / track_delete on other tasks — the shifting/growing
// backing array can invalidate the returned pointer.  Only call
// these when you know no other task can mutate the track store
// (e.g. boot-time single-task init).  Prefer the copy-out APIs
// further down for any hot-path / multi-task caller.
//
// See track_copy_by_id / track_copy_at / track_snapshot_count /
// track_auto_detect_copy for the thread-safe variants.

// Number of tracks currently loaded in memory.
int track_count();

// Get track by index (0-based). Returns nullptr if out of range.
const TrackDefinition* track_get(int index);

// Get track by ID string (e.g. "track_001"). Returns nullptr if not found.
const TrackDefinition* track_get_by_id(const char* id);

// Find the nearest track whose center is within 5 km of (lat, lon).
// Returns nullptr if no track is within range.
const TrackDefinition* track_auto_detect(double lat, double lon);

// Return up to max_results nearby tracks sorted by ascending distance.
// Distances are measured to each track center.
int track_find_nearby(double lat, double lon,
                      NearbyTrackCandidate* out, int max_results);

// Distance from the current position to a track center in meters.
double track_distance_to_center_m(const TrackDefinition* track,
                                  double lat, double lon);

// Load the first available track into *out.
// Returns true if at least one track exists.
bool track_load_first(TrackDefinition* out);

// --- Thread-safe copy-out APIs ---
//
// Each of these takes track_store_mutex internally, copies the
// result into the caller's buffer, and releases.  Safe against
// concurrent track_save / track_delete mutations.
//
// See src/track/track_internal.h for the lock-ordering rules
// (session_mutex is OUTSIDE track_store_mutex).

// Look up a track by id and copy it into `out`.  Returns true on
// success, false if no track with that id exists or `out` is null.
bool track_copy_by_id(const char* id, TrackDefinition* out);

// Copy the track at position `index` into `out`.  Returns false if
// the index is out of range AT THE MOMENT the mutex is held.
bool track_copy_at(int index, TrackDefinition* out);

// Snapshot the track count under the mutex.  Use paired with
// track_copy_at for iteration — but note the count can change
// between calls, so a loop `for (i=0; i<count; i++) track_copy_at(i)`
// can see a shorter-than-count list if a delete races; callers must
// handle the `false` return from track_copy_at gracefully.
int track_snapshot_count();

// Auto-detect the nearest track to (lat, lon) and copy it into
// `out`.  Returns true if a track was found within
// AUTO_DETECT_MAX_M, false otherwise.
bool track_auto_detect_copy(double lat, double lon,
                            TrackDefinition* out);

// --- Mutation ---

// Save a track to SD:/tracks/track_NNN.json and add to memory.
// The track ID is auto-generated (max existing NNN + 1).
// Returns true on success.
bool track_save(const TrackDefinition* track);
TrackSaveResult track_save_detailed(const TrackDefinition* track);

// Delete a track by ID from both SD card and memory.
// Returns true if found and deleted.
bool track_delete(const char* id);
