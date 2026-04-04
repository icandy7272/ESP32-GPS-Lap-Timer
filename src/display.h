#pragma once

// ============================================================
// TFT Display Module — ESP32-S3 GPS Lap Timer
// Renders three screens: Driving, Status, Lap List.
// Runs on Core 1, priority 10, 8192 byte stack.
// See docs/ARCHITECTURE.md and docs/PRD.md section 6.
// ============================================================

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// --- Public API ---

// Initialise TFT hardware, backlight, and start the display task.
// btn_display_q: receives ButtonEvent from button task
// spi_mtx:       shared SPI bus mutex (SD + TFT)
// session_mtx:   protects SessionState reads
void display_init(QueueHandle_t     btn_display_q,
                  SemaphoreHandle_t spi_mtx,
                  SemaphoreHandle_t session_mtx);

// FreeRTOS task entry point (Core 1, priority 10, stack 8192).
// param is unused — dependencies injected via display_init().
void display_task(void* param);

// --- Boot sequence screens (called from setup() before display_task starts) ---
// These draw directly to the TFT. No mutex needed since display_task
// hasn't started yet. TFT must be initialised first via display_init().

void display_show_splash();                     // "GPS Lap Timer v1.0"
void display_show_gps_search(int sats);         // "GPS Searching... X sats"
void display_show_track_found(const char* name); // "Track: XX"
void display_show_ready();                       // "READY" in big green text
