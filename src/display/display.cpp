// ============================================================
// TFT Display Module — ESP32-S3 GPS Lap Timer
// Three screens: Driving, Status, Lap List.
// Partial refresh via TFT_eSprite for flicker-free delta.
// SPI mutex with 10ms timeout — skips frame on contention.
// ============================================================

#include "display.h"
#include "display_internal.h"
#include "session.h"
#include "config.h"
#include "types.h"
#include "pins.h"
#include "boot_sequence.h"
#include "gps/gps_filter.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "../sdfat_global.h"
#include <esp_timer.h>

// Speed thresholds for screen lock (km/h)
static constexpr float SPEED_LOCK_THRESHOLD   = 15.0f;
static constexpr float SPEED_UNLOCK_THRESHOLD = 5.0f;
static constexpr int   UNLOCK_HOLD_MS         = 3000;

// ============================================================
// Module state (file-scoped, no globals leak)
// ============================================================

TFT_eSPI    s_tft = TFT_eSPI();
TFT_eSprite s_delta_sprite(&s_tft);
static bool s_tft_ready = false;
static bool s_backlight_ready = false;
static bool s_boot_frame_ready = false;

static QueueHandle_t     s_btn_queue   = nullptr;
static SemaphoreHandle_t s_spi_mtx     = nullptr;
static SemaphoreHandle_t s_session_mtx = nullptr;

static ScreenId    s_current_screen  = SCREEN_DRIVING;
static bool        s_screen_locked   = false;
static uint32_t    s_slow_since_ms   = 0;  // millis() when speed dropped below threshold
static bool        s_below_threshold = false;

static SessionState s_cached_state;    // snapshot taken under mutex each frame
static SessionState s_prev_state;      // previous frame snapshot for dirty detection
static DirtyFlags   s_dirty;

int s_lap_list_scroll = 0;      // scroll offset for lap list

// Cached SD free space (refreshed periodically, outside SPI render lock)
float    s_sd_free_gb     = -1.0f; // negative = not yet queried
static uint32_t s_sd_query_ms    = 0;
static constexpr uint32_t SD_QUERY_INTERVAL_MS = 10000; // refresh every 10s

// Firmware version (displayed on status screen).
// Injected at build time by tools/inject_version.py from VERSION + git SHA;
// fallback covers direct compiler runs (e.g. host tests) where the
// platformio script is not active.
#ifndef FW_VERSION_STR
#define FW_VERSION_STR "v0.0.0-nobuildtag"
#endif
const char* FW_VERSION = FW_VERSION_STR;

// ============================================================
// Snapshot session state under mutex (non-blocking)
// ============================================================

static bool snapshot_session_state() {
    if (xSemaphoreTake(s_session_mtx, pdMS_TO_TICKS(SPI_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }
    memcpy(&s_cached_state, &session_state, sizeof(SessionState));
    xSemaphoreGive(s_session_mtx);
    return true;
}

static void apply_display_gps_fix() {
    GpsPoint display_fix = {};
    if (!gps_filter_get_display_fix(&display_fix)) {
        return;
    }

    s_cached_state.gps_fix_ok = display_fix.fix_3d;
    s_cached_state.gps_satellites = display_fix.satellites;
    s_cached_state.gps_lat_deg = display_fix.lat_deg;
    s_cached_state.gps_lon_deg = display_fix.lon_deg;
    s_cached_state.speed_kmh = display_fix.speed_kmh;
}

// ============================================================
// Dirty flag computation
// ============================================================

static DirtyFlags compute_dirty(const SessionState& cur,
                                const SessionState& prev,
                                bool screen_changed) {
    DirtyFlags d = {};
    d.full_redraw  = screen_changed;
    // Delta area content is state-dependent, so use state-aware triggers:
    //   IDLE     → sats + fix (ignore speed jitter while stationary)
    //   OUT_LAP  → speed + fix
    //   NORMAL   → delta fields only; do NOT trigger on current_lap++,
    //              because lap_timer only refreshes delta_ms on the next
    //              GPS fix — an immediate repaint would show stale data.
    // State transitions always trigger a full redraw.
    DrivingState cur_state  = get_driving_state(cur);
    DrivingState prev_state = get_driving_state(prev);
    d.delta = (cur_state != prev_state);
    switch (cur_state) {
        case DRIVING_IDLE:
            d.delta = d.delta
                    || (cur.gps_satellites != prev.gps_satellites)
                    || (cur.gps_fix_ok != prev.gps_fix_ok)
                    || (strncmp(cur.track_name, prev.track_name,
                                sizeof(cur.track_name)) != 0);
            break;
        case DRIVING_OUT_LAP:
            d.delta = d.delta
                    || (cur.gps_fix_ok != prev.gps_fix_ok)
                    || ((int)(cur.speed_kmh + 0.5f)
                        != (int)(prev.speed_kmh + 0.5f));
            break;
        case DRIVING_NORMAL:
            d.delta = d.delta
                    || (cur.delta_ms != prev.delta_ms)
                    || (cur.delta_valid != prev.delta_valid)
                    || (cur.off_track != prev.off_track)
                    || (cur.gps_fix_ok != prev.gps_fix_ok);
            break;
    }
    d.lap_number   = (cur.current_lap != prev.current_lap)
                   || (cur.is_recording != prev.is_recording);
    d.current_time = true;  // always update running timer
    d.best_time    = (cur.best_lap_time_ms != prev.best_lap_time_ms);
    d.gps_info     = (cur.gps_satellites != prev.gps_satellites)
                   || (cur.gps_fix_ok != prev.gps_fix_ok);
    d.off_track    = (cur.off_track != prev.off_track);
    // A driving-state transition changes the effective bg colour too
    // (IDLE/OUT_LAP are always neutral, NORMAL uses delta_background_colour).
    // Force a background redraw so every region — top bar, center sprite,
    // bottom bar — rewrites its fill with the new colour.
    d.background   = (delta_background_colour(cur) !=
                      delta_background_colour(prev))
                   || (cur_state != prev_state);
    return d;
}

// ============================================================
// Screen lock logic (lock to driving screen at speed)
// ============================================================

// Idle auto-switch: after 10s below 5 km/h, jump to lap list
static constexpr uint32_t IDLE_SWITCH_MS = 10000;
static uint32_t s_idle_start_ms = 0;

static void update_screen_lock(float speed_kmh, uint32_t now_ms) {
    if (speed_kmh > SPEED_LOCK_THRESHOLD) {
        s_screen_locked   = true;
        s_below_threshold = false;
        s_slow_since_ms   = 0;
        s_idle_start_ms   = 0;
        s_current_screen  = SCREEN_DRIVING;
        return;
    }

    if (speed_kmh < SPEED_UNLOCK_THRESHOLD) {
        if (!s_below_threshold) {
            s_below_threshold = true;
            s_slow_since_ms   = now_ms;
        }
        if (s_screen_locked &&
            (now_ms - s_slow_since_ms >= UNLOCK_HOLD_MS)) {
            s_screen_locked = false;
        }
        // Auto-switch to lap list after sustained idle
        if (s_idle_start_ms == 0) {
            s_idle_start_ms = now_ms;
        }
        // Auto-switch disabled until buttons are connected
        // if ((now_ms - s_idle_start_ms) > IDLE_SWITCH_MS &&
        //     s_current_screen == SCREEN_DRIVING) {
        //     s_current_screen = SCREEN_LAP_LIST;
        //     s_idle_start_ms = 0;
        // }
    } else {
        s_below_threshold = false;
        s_idle_start_ms   = 0;
    }
}

// ============================================================
// Button handling — cycle screens on short press
// ============================================================

static bool handle_button_events() {
    ButtonEvent evt;
    bool screen_changed = false;

    while (xQueueReceive(s_btn_queue, &evt, 0) == pdTRUE) {
        if (evt.button_id != BUTTON_SECTOR || s_screen_locked) {
            continue;
        }

        // Long press on lap list: exit to next screen
        if (evt.event_type == BUTTON_LONG_PRESS &&
            s_current_screen == SCREEN_LAP_LIST) {
            s_current_screen = SCREEN_DRIVING;
            s_lap_list_scroll = 0;
            screen_changed = true;
            continue;
        }

        if (evt.event_type != BUTTON_SHORT_PRESS) {
            continue;
        }

        // On lap list screen: scroll within list instead of switching
        if (s_current_screen == SCREEN_LAP_LIST) {
            s_lap_list_scroll += LAPS_PER_PAGE;
            if (s_lap_list_scroll >= s_cached_state.lap_count) {
                s_lap_list_scroll = 0;
            }
            screen_changed = true;
            continue;
        }

        // Other screens: cycle to next screen
        uint8_t next = (static_cast<uint8_t>(s_current_screen) + 1)
                       % SCREEN_COUNT;
        s_current_screen = static_cast<ScreenId>(next);
        s_lap_list_scroll = 0;
        screen_changed = true;
    }
    return screen_changed;
}

// ============================================================
// SPI-guarded draw dispatch
// ============================================================

bool s_status_needs_clear = true;
bool s_laplist_needs_clear = true;
bool s_laplist_header_drawn = false;

// Sticky flag: if a full_redraw clear pass completed but the content draw
// was skipped (SPI timeout), force full_redraw again next frame.
static bool s_pending_full_redraw = false;

static void render_frame(bool screen_changed) {
    DirtyFlags df = compute_dirty(s_cached_state, s_prev_state,
                                  screen_changed);

    // Carry forward a pending full redraw from a previous failed frame
    if (s_pending_full_redraw) {
        df.full_redraw = true;
        s_pending_full_redraw = false;
    }

    // On screen change, force clear for status/laplist screens
    if (df.full_redraw) {
        s_status_needs_clear   = true;
        s_laplist_needs_clear  = true;
        s_laplist_header_drawn = false;
    }

    // Phase 1: clear pass (if needed) — release SPI between phases
    // so storage_task can write VBO data and INT_WDT doesn't fire.
    if (df.full_redraw) {
        if (xSemaphoreTake(s_spi_mtx, pdMS_TO_TICKS(SPI_TIMEOUT_MS)) != pdTRUE) {
            s_pending_full_redraw = true;  // retry next frame
            return;
        }
        s_tft.fillScreen(TFT_BLACK);
        xSemaphoreGive(s_spi_mtx);
        taskYIELD();  // let storage_task / wifi_task run
    }

    // Phase 2: content draw
    if (xSemaphoreTake(s_spi_mtx, pdMS_TO_TICKS(SPI_TIMEOUT_MS)) != pdTRUE) {
        if (df.full_redraw) {
            // Screen was cleared but content wasn't drawn — mark as pending
            // so the next frame retries the full redraw.
            s_pending_full_redraw = true;
        }
        return;
    }

    switch (s_current_screen) {
        case SCREEN_DRIVING:
            draw_driving_screen(df, s_cached_state);
            break;
        case SCREEN_STATUS:
            draw_status_screen(s_cached_state);
            break;
        case SCREEN_LAP_LIST:
            draw_lap_list_screen(s_cached_state);
            break;
        default:
            break;
    }

    xSemaphoreGive(s_spi_mtx);

    s_prev_state = s_cached_state;
}

// ============================================================
// Hardware init (backlight PWM + TFT)
// ============================================================

static void init_backlight() {
    ledcSetup(0, 1000, 8);            // channel 0, 1 kHz, 8-bit
    ledcAttachPin(PIN_TFT_BL, 0);     // attach GPIO to channel 0
    // Use configured brightness (0-255), default 200
    extern AppConfig app_config;
    ledcWrite(0, app_config.brightness);
}

static void reset_tft_hardware() {
    // Keep other SPI clients off the bus and hold the backlight low
    // while the panel controller is being reset on cold power-on.
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);
    pinMode(PIN_TFT_CS, OUTPUT);
    digitalWrite(PIN_TFT_CS, HIGH);
    pinMode(PIN_TFT_DC, OUTPUT);
    digitalWrite(PIN_TFT_DC, HIGH);
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, LOW);

    pinMode(PIN_TFT_RST, OUTPUT);
    digitalWrite(PIN_TFT_RST, LOW);
    delay(boot_tft_reset_low_ms());
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(boot_tft_reset_high_ms());
}

static void init_tft_once() {
    reset_tft_hardware();
    s_tft.init();
    s_tft.setRotation(1);  // landscape 320x240
    s_tft.fillScreen(TFT_BLACK);
}

static void init_tft() {
    // Some breadboard cold boots appear to miss the first panel init even
    // though setup continues. A second reset+init pass is cheap and gives
    // the controller another clean chance to latch commands.
    init_tft_once();
    delay(50);
    init_tft_once();
}

static void init_delta_sprite() {
    s_delta_sprite.createSprite(SCREEN_W, DELTA_AREA_H);
    s_delta_sprite.setTextDatum(MC_DATUM);
}

static void ensure_tft_ready() {
    if (s_tft_ready) {
        return;
    }
    init_tft();
    s_tft_ready = true;
}

static void ensure_backlight_ready() {
    if (s_backlight_ready) {
        return;
    }
    init_backlight();
    s_backlight_ready = true;
}

// ============================================================
// Public API
// ============================================================

void display_boot_init() {
    ensure_tft_ready();
    if (!s_boot_frame_ready) {
        draw_boot_static_frame();
        s_boot_frame_ready = true;
    }
    ensure_backlight_ready();
}

void display_init(QueueHandle_t     btn_display_q,
                  SemaphoreHandle_t spi_mtx,
                  SemaphoreHandle_t session_mtx) {
    s_btn_queue   = btn_display_q;
    s_spi_mtx     = spi_mtx;
    s_session_mtx = session_mtx;

    ensure_tft_ready();
    ensure_backlight_ready();
    init_delta_sprite();

    memset(&s_cached_state, 0, sizeof(s_cached_state));
    s_cached_state.best_lap_time_ms = -1;
    s_cached_state.best_lap_number  = -1;

    memset(&s_prev_state, 0, sizeof(s_prev_state));
    s_prev_state.best_lap_time_ms = -1;
    s_prev_state.best_lap_number  = -1;

    s_dirty.full_redraw = true;
}

// ============================================================
// Display task (FreeRTOS)
// ============================================================

void display_task(void* param) {
    (void)param;

    TickType_t last_wake = xTaskGetTickCount();

    // Force full redraw on first frame
    bool first_frame = true;

    extern int crash_bc_core1;

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FRAME_INTERVAL_MS));

        crash_bc_core1 = 60;  // display: frame start
        // 1. Snapshot session state (skip frame if mutex busy)
        if (!snapshot_session_state()) {
            continue;
        }
        apply_display_gps_fix();

        // 2. Handle button input, determine if screen changed
        bool screen_changed = handle_button_events() || first_frame;

        // 3. Refresh SD free space periodically (outside SPI render lock)
        uint32_t now_ms = millis();
        if (s_current_screen == SCREEN_STATUS &&
            (now_ms - s_sd_query_ms >= SD_QUERY_INTERVAL_MS || s_sd_free_gb < 0)) {
            if (xSemaphoreTake(s_spi_mtx, pdMS_TO_TICKS(SPI_TIMEOUT_MS)) == pdTRUE) {
                if (sd.vol() != nullptr) {
                    uint64_t fb = (uint64_t)sd.vol()->freeClusterCount()
                                * (uint64_t)sd.vol()->bytesPerCluster();
                    s_sd_free_gb = (float)(fb / (1024ULL * 1024ULL)) / 1024.0f;
                } else {
                    s_sd_free_gb = -1.0f;  // SD not mounted
                }
                xSemaphoreGive(s_spi_mtx);
                s_sd_query_ms = now_ms;
            }
        }

        // 4. Update screen lock based on GPS speed
        float speed_kmh = s_cached_state.speed_kmh;
        update_screen_lock(speed_kmh, now_ms);

        // 5. Render the active screen
        crash_bc_core1 = 61;  // display: about to render
        render_frame(screen_changed);
        first_frame = false;  // only clear after render succeeds
        crash_bc_core1 = 62;  // display: frame done
    }
}
