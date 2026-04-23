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

struct BootStatus;

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

// Wake the display task RIGHT NOW for a render pass, skipping the
// remaining heartbeat wait.  Called from lap_timer_delta when a
// fresh GPS fix has updated session_state.delta_ms so the driver
// sees the +/- delta digit change within 20-30 ms of the fix
// arriving instead of waiting up to FRAME_INTERVAL_MS (50 ms) for
// the next tick.
//
// Safe to call from any task.  A kick that arrives while the
// display task is mid-render is coalesced into a single notification
// count, so rapid consecutive kicks can't queue more than one
// pending render.  Before display_init() has captured the task
// handle (i.e. during boot before display_task starts), this is a
// cheap no-op.
void display_kick();

// --- Boot sequence renderer (called from setup() before display_task starts) ---
// These draw directly to the TFT. No mutex needed since display_task
// hasn't started yet.

// Initialize TFT + boot static frame (black background + centered logo).
void display_boot_init();
// Update only mutable boot status UI regions.
void display_boot_update(const BootStatus& status);

// Compatibility wrappers that adapt legacy boot call sites to the unified
// splash renderer while migration is in progress.
void display_show_splash();
void display_show_gps_search(int sats);
void display_show_track_found(const char* name);
void display_show_recovery();
void display_show_ready();
