#pragma once

// ============================================================
// Internal shared declarations for wifi/ module.
// Not part of the public API — only files under src/wifi/
// should include this header.
// ============================================================

#include <Arduino.h>
#include <WebServer.h>

#include "types.h"

// --- Extern references (created in main.cpp) ---
extern SemaphoreHandle_t spi_mutex;
extern SemaphoreHandle_t session_mutex;
extern SessionState      session_state;

// --- HTTP server on port 80 (defined in wifi_server.cpp) ---
extern WebServer server;

// --- Recording-mode throttle state (defined in wifi_server.cpp) ---
extern unsigned long last_request_ms;

// --- Shared constants ---
inline constexpr unsigned long RECORDING_THROTTLE_MS = 500;
inline constexpr size_t        FILE_CHUNK_SIZE       = 4096;
inline constexpr unsigned long CHUNK_DELAY_MS        = 10;

// --- Shared helpers ---

// Throttle helper — returns true if request should be rejected
// due to recording-mode throttling.
bool is_throttled();

// Thread-safe snapshot of the global session_state.
SessionState read_session_state();

// Extract a string from JSON by key. Writes to out, returns false if missing.
// Defined in api_tracks.cpp.
bool json_extract_str(const char* json, const char* key,
                      char* out, int out_len);

// --- Route handlers ---
// Registered in wifi_server.cpp::wifi_init(); defined in their
// respective api_*.cpp / web_ui.cpp files.
void handle_root();
void handle_api_status();
void handle_api_sessions();
void handle_api_tracks();
void handle_api_tracks_post();
void handle_api_tracks_select();
void handle_api_tracks_delete();
void handle_api_recording();
void handle_api_settings_get();
void handle_api_settings_post();
void handle_not_found();
