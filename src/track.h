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
// Tri-state return so callers can distinguish "the store was busy,
// retry" from "the id doesn't exist".  session_try_claim_track_switch
// maps BUSY → SESSION_TRACK_SWITCH_CONTENDED (HTTP 503) and NOT_FOUND
// → SESSION_TRACK_SWITCH_NOT_FOUND (HTTP 404); conflating them (the
// previous `bool` return) made 404 misattribute to real misses when
// the store was just contended (codex review 2026-04-22 Medium).
//
// See src/track/track_internal.h for the lock-ordering rules
// (session_mutex is OUTSIDE track_store_mutex).
typedef enum {
    TRACK_LOOKUP_OK = 0,
    TRACK_LOOKUP_BUSY,       // track_store_mutex timeout
    TRACK_LOOKUP_NOT_FOUND,  // id missing / index out of range / nothing near
} TrackLookupResult;

// Look up a track by id and copy it into `out`.  OK on success,
// NOT_FOUND if the id doesn't exist or `out` is null, BUSY on
// mutex timeout.
TrackLookupResult track_copy_by_id(const char* id, TrackDefinition* out);

// Copy the track at position `index` into `out`.
TrackLookupResult track_copy_at(int index, TrackDefinition* out);

// Snapshot the track count under the mutex.  Returns -1 on mutex
// timeout so callers that use it as an iteration bound can bail
// gracefully; a count of 0 just means the array is empty.
int track_snapshot_count();

// Auto-detect the nearest track to (lat, lon) and copy it into
// `out`.  NOT_FOUND if no track is within AUTO_DETECT_MAX_M.
TrackLookupResult track_auto_detect_copy(double lat, double lon,
                                         TrackDefinition* out);

// --- Mutation ---

// Save a track to SD:/tracks/track_NNN.json and add to memory.
// The track ID is auto-generated (max existing NNN + 1).
// Returns true on success.
bool track_save(const TrackDefinition* track);

// Save a track + return the saved copy (including the generated id)
// via `out_saved` so callers don't have to race on
// `track_get(track_count()-1)` to find what they just saved.  The
// previous "peek at last slot" pattern could pick the wrong track
// if another task saved in between.  `out_saved` may be nullptr.
//
// track_store_mutex is held across the entire save — including the
// SD write — so two concurrent saves can't assign the same id.  On
// a busy card the write can take hundreds of ms; readers stall
// during that window, but saves are rare (user creates tracks
// occasionally, not per fix).
TrackSaveResult track_save_detailed(const TrackDefinition* track,
                                    TrackDefinition* out_saved);

// Delete a track by ID from both SD card and memory.
// Returns true if found and deleted.
bool track_delete(const char* id);
