#pragma once

#include "types.h"
#include "boot_status.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// ============================================================
// Layout constants (320x240 landscape)
// ============================================================

static constexpr int SCREEN_W = 320;
static constexpr int SCREEN_H = 240;

// Driving screen regions
static constexpr int INFO_BAR_H      = 24;   // top bar height
static constexpr int BOTTOM_BAR_H    = 20;   // bottom bar height
static constexpr int DELTA_AREA_Y    = INFO_BAR_H;
static constexpr int DELTA_AREA_H    = SCREEN_H - INFO_BAR_H - BOTTOM_BAR_H;

// Lap list screen
static constexpr int LAP_ROW_H       = 28;   // pixels per lap row
static constexpr int LAP_HEADER_H    = 30;   // header row height
static constexpr int LAPS_PER_PAGE   = (SCREEN_H - LAP_HEADER_H) / LAP_ROW_H;

// Timing
static constexpr int FRAME_INTERVAL_MS = 100; // 10 FPS target
static constexpr int SPI_TIMEOUT_MS    = 10;

// ============================================================
// Screen enumeration
// ============================================================

enum ScreenId : uint8_t {
    SCREEN_DRIVING  = 0,
    SCREEN_STATUS   = 1,
    SCREEN_LAP_LIST = 2,
    SCREEN_COUNT    = 3,
};

// ============================================================
// Dirty flags — track which fields changed since last draw
// ============================================================

struct DirtyFlags {
    bool full_redraw;
    bool delta;
    bool lap_number;
    bool current_time;
    bool best_time;
    bool gps_info;
    bool off_track;
    bool background;
};

// ============================================================
// Module state (file-scoped, no globals leak)
// ============================================================

extern TFT_eSPI    s_tft;
extern TFT_eSprite s_delta_sprite;

extern int s_lap_list_scroll;      // scroll offset for lap list

// Cached SD free space (refreshed periodically, outside SPI render lock)
extern float    s_sd_free_gb;      // negative = not yet queried

// Firmware version (displayed on status screen)
extern const char* FW_VERSION;

extern bool s_status_needs_clear;

extern bool s_laplist_needs_clear;

extern bool s_laplist_header_drawn;

// ---- Colour helpers ----

// The ILI9341 panel runs in BGR mode (TFT_RGB_ORDER=1).
// TFT_eSPI colour constants are always RGB565, so R and B appear
// swapped on screen.  Use this wrapper for any colour that has a
// visible R or B component.
static inline uint16_t bgr565(uint16_t rgb) {
    uint16_t r = (rgb >> 11) & 0x1F;
    uint16_t g = (rgb >> 5)  & 0x3F;
    uint16_t b = rgb & 0x1F;
    return (b << 11) | (g << 5) | r;
}

// ---- Cross-file helpers ----

uint16_t delta_background_colour(const SessionState& st);
uint16_t lap_status_colour(uint8_t status);

// Format milliseconds as "m:ss.xx" into buf (must be >= 12 chars).
void format_lap_time(char* buf, size_t len, int32_t time_ms);

// Format delta_ms as "+0.35" or "-1.22" into buf (must be >= 10 chars).
void format_delta(char* buf, size_t len, int32_t delta_ms);

// ---- Driving screen state (shared with compute_dirty) ----

enum DrivingState : uint8_t {
    DRIVING_IDLE    = 0,  // !is_recording
    DRIVING_OUT_LAP = 1,  // recording, before first start-line crossing
    DRIVING_NORMAL  = 2,  // recording, after first crossing
};

// Keep this inline so compute_dirty() and draw_driving_delta() stay in sync.
// Depends only on SessionState fields already set by session.cpp:
// the first line crossing (handle_lap_finish, s_lap_start_us == 0) bumps
// current_lap from 0 to 1.
static inline DrivingState get_driving_state(const SessionState& st) {
    if (!st.is_recording) return DRIVING_IDLE;
    if (st.current_lap == 0) return DRIVING_OUT_LAP;
    return DRIVING_NORMAL;
}

// ---- Per-screen render entry points ----

void draw_driving_screen(const DirtyFlags& df, const SessionState& st);
void draw_status_screen(const SessionState& st);
void draw_lap_list_screen(const SessionState& st);

// Boot renderer internals.
void draw_boot_static_frame();
void draw_boot_status(const BootStatus& status);
