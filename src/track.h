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

// Maximum number of tracks held in memory.
static constexpr int MAX_TRACKS = 20;

// --- Initialization ---
// Scan SD:/tracks/ for track_*.json files, parse each into
// a TrackDefinition, and store in a static array.
// Returns true if SD access succeeded (0 tracks is still OK).
bool track_init();

// --- Queries ---

// Number of tracks currently loaded in memory.
int track_count();

// Get track by index (0-based). Returns nullptr if out of range.
const TrackDefinition* track_get(int index);

// Get track by ID string (e.g. "track_001"). Returns nullptr if not found.
const TrackDefinition* track_get_by_id(const char* id);

// Find the nearest track whose center is within 5 km of (lat, lon).
// Returns nullptr if no track is within range.
const TrackDefinition* track_auto_detect(double lat, double lon);

// Load the first available track into *out.
// Returns true if at least one track exists.
bool track_load_first(TrackDefinition* out);

// --- Mutation ---

// Save a track to SD:/tracks/track_NNN.json and add to memory.
// The track ID is auto-generated (max existing NNN + 1).
// Returns true on success.
bool track_save(const TrackDefinition* track);

// Delete a track by ID from both SD card and memory.
// Returns true if found and deleted.
bool track_delete(const char* id);
