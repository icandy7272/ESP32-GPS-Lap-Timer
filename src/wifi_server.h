#pragma once

#include <stddef.h>

// ============================================================
// WiFi AP + HTTP server — ESP32-S3 GPS Lap Timer
// Provides web dashboard, VBO download, track/settings API.
// See docs/ARCHITECTURE.md for task scheduling details.
// ============================================================

// Initialise WiFi AP mode and register HTTP routes.
// Call once from setup(), before creating the WiFi task.
void wifi_init();

// FreeRTOS task entry point.
// Core 1, priority 5, stack 8192 bytes.
void wifi_task(void* param);

// Best-effort UDP broadcast of a [gps-live] line on the AP subnet.
// Intended to mirror the line that gps_fix.cpp also writes to Serial,
// so tools/live_map.py can run over WiFi (battery-powered field tests)
// without giving up its byte-for-byte parser.
//
// `line` should be a complete textual line including its trailing
// newline.  `len` is the number of bytes in `line` (no NUL terminator
// expected).  Failures are silent — if WiFi is not yet up, or the
// underlying UDP send fails, the call is a no-op so the GPS task
// never blocks on networking.
void gps_live_udp_broadcast(const char* line, size_t len);
