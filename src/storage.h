#pragma once

// ============================================================
// SD card storage module — ESP32-S3 GPS Lap Timer
// Manages VBO file writing, power-loss recovery, and session
// file listing. Uses SdFat on shared SPI bus with spi_mutex.
// See docs/ARCHITECTURE.md sections 5 and 7.
// ============================================================

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

// --- Initialization ---
// Mount SD card via SPI, create /sessions/ and /tracks/ dirs,
// scan root for _recording.vbo.tmp and run power-loss recovery.
// Returns true on success.
bool storage_init();

// --- FreeRTOS task ---
// Blocks on vbo_write_queue, formats VboEntry to VBO data line,
// writes to .tmp file, periodic fsync every 30 s.
// Pin to Core 1, priority 18, stack 6144.
void storage_task(void* param);

// --- Session lifecycle ---
// Create _recording.vbo.tmp with VBO header sections.
// track_name is written into [session data] and used in final filename.
bool storage_start_session(const char* track_name);

// Flush, sync, close .tmp file and rename to final .vbo name.
void storage_end_session();

// --- Lap timing ---
// Append a lap timing line to the [laptiming] section.
// Performs immediate fflush + fsync after write.
void storage_write_lap_timing(const LapRecord* lap);

// --- Session listing ---
// Populate names[] with up to max session filenames from /sessions/.
// Returns number of sessions found.
int storage_list_sessions(char names[][64], int max);

// Get full path for a session file name.
// Returns true if the file exists.
bool storage_get_session_path(const char* name, char* path, int path_len);

// --- Status ---
// Bytes written to current session (for display/diagnostics).
uint32_t storage_get_bytes_written();
