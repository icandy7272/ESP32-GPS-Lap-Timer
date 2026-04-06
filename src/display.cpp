// ============================================================
// TFT Display Module — ESP32-S3 GPS Lap Timer
// Three screens: Driving, Status, Lap List.
// Partial refresh via TFT_eSprite for flicker-free delta.
// SPI mutex with 10ms timeout — skips frame on contention.
// ============================================================

#include "display.h"
#include "session.h"
#include "config.h"
#include "types.h"
#include "pins.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SdFat.h>
#include <esp_timer.h>

// ---- External shared resource for SD free space query -------
extern SdFat sd;

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

// Speed thresholds for screen lock (km/h)
static constexpr float SPEED_LOCK_THRESHOLD   = 15.0f;
static constexpr float SPEED_UNLOCK_THRESHOLD = 5.0f;
static constexpr int   UNLOCK_HOLD_MS         = 3000;

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

static TFT_eSPI    s_tft = TFT_eSPI();
static TFT_eSprite s_delta_sprite(&s_tft);

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

static int s_lap_list_scroll = 0;      // scroll offset for lap list

// Cached SD free space (refreshed periodically, outside SPI render lock)
static float    s_sd_free_gb     = -1.0f; // negative = not yet queried
static uint32_t s_sd_query_ms    = 0;
static constexpr uint32_t SD_QUERY_INTERVAL_MS = 10000; // refresh every 10s

// Firmware version (displayed on status screen)
static const char* FW_VERSION = "v1.0.0";

// ============================================================
// Colour helpers
// ============================================================

static uint16_t delta_background_colour(const SessionState& st) {
    if (!st.gps_fix_ok)  return TFT_DARKGREY;
    if (st.off_track)    return TFT_YELLOW;
    if (!st.delta_valid) return TFT_DARKGREY;
    if (st.delta_ms < 0) return TFT_GREEN;
    if (st.delta_ms > 0) return TFT_RED;
    return TFT_DARKGREY;
}

static uint16_t lap_status_colour(uint8_t status) {
    switch (status) {
        case LAP_STATUS_TIMED:  return TFT_WHITE;
        case LAP_STATUS_NO_REF: return TFT_CYAN;
        case LAP_STATUS_SLOW:   return TFT_ORANGE;
        case LAP_STATUS_SHORT:  return TFT_ORANGE;
        case LAP_STATUS_OUT:    return TFT_DARKGREY;
        default:                return TFT_WHITE;
    }
}

// ============================================================
// Time formatting helpers
// ============================================================

// Format milliseconds as "m:ss.xx" into buf (must be >= 12 chars).
static void format_lap_time(char* buf, size_t len, int32_t time_ms) {
    if (time_ms < 0) {
        snprintf(buf, len, "--:--.--");
        return;
    }
    int mins    = time_ms / 60000;
    int secs    = (time_ms % 60000) / 1000;
    int hundths = (time_ms % 1000) / 10;
    snprintf(buf, len, "%d:%02d.%02d", mins, secs, hundths);
}

// Format delta_ms as "+0.35" or "-1.22" into buf (must be >= 10 chars).
static void format_delta(char* buf, size_t len, int32_t delta_ms) {
    const char sign = (delta_ms >= 0) ? '+' : '-';
    int abs_ms = abs(delta_ms);
    int secs   = abs_ms / 1000;
    int hundr  = (abs_ms % 1000) / 10;
    snprintf(buf, len, "%c%d.%02d", sign, secs, hundr);
}

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

// ============================================================
// Dirty flag computation
// ============================================================

static DirtyFlags compute_dirty(const SessionState& cur,
                                const SessionState& prev,
                                bool screen_changed) {
    DirtyFlags d = {};
    d.full_redraw  = screen_changed;
    d.delta        = (cur.delta_ms != prev.delta_ms)
                   || (cur.delta_valid != prev.delta_valid)
                   || (cur.off_track != prev.off_track)
                   || (cur.gps_fix_ok != prev.gps_fix_ok);
    d.lap_number   = (cur.current_lap != prev.current_lap);
    d.current_time = true;  // always update running timer
    d.best_time    = (cur.best_lap_time_ms != prev.best_lap_time_ms);
    d.gps_info     = (cur.gps_satellites != prev.gps_satellites)
                   || (cur.gps_fix_ok != prev.gps_fix_ok);
    d.off_track    = (cur.off_track != prev.off_track);
    d.background   = (delta_background_colour(cur) !=
                      delta_background_colour(prev));
    return d;
}

// ============================================================
// Screen lock logic (lock to driving screen at speed)
// ============================================================

static void update_screen_lock(float speed_kmh, uint32_t now_ms) {
    if (speed_kmh > SPEED_LOCK_THRESHOLD) {
        s_screen_locked   = true;
        s_below_threshold = false;
        s_slow_since_ms   = 0;
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
    } else {
        s_below_threshold = false;
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
// Driving screen rendering
// ============================================================

static void draw_driving_top_bar(const SessionState& st) {
    s_tft.setTextDatum(TL_DATUM);
    s_tft.setTextColor(TFT_WHITE, delta_background_colour(st));

    char lap_buf[8];
    snprintf(lap_buf, sizeof(lap_buf), "L%d", st.current_lap);
    s_tft.drawString(lap_buf, 4, 4, 2);
}

static void draw_driving_current_time(const SessionState& st) {
    s_tft.setTextDatum(TR_DATUM);
    s_tft.setTextColor(TFT_WHITE, delta_background_colour(st));

    // Real elapsed time from lap start (not synthetic best+delta)
    char time_buf[12];
    int32_t elapsed_ms = 0;  // show "0:00.00" before first lap starts
    if (st.current_lap_start_us > 0) {
        elapsed_ms = (int32_t)((esp_timer_get_time() - st.current_lap_start_us) / 1000);
    }
    format_lap_time(time_buf, sizeof(time_buf), elapsed_ms);
    s_tft.drawString(time_buf, SCREEN_W - 4, 4, 2);
}

static void draw_driving_delta(const SessionState& st) {
    uint16_t bg = delta_background_colour(st);

    s_delta_sprite.fillSprite(bg);
    s_delta_sprite.setTextDatum(MC_DATUM);
    s_delta_sprite.setTextColor(TFT_WHITE, bg);

    if (!st.gps_fix_ok) {
        s_delta_sprite.drawString("NO GPS",
                                  SCREEN_W / 2, DELTA_AREA_H / 2, 4);
    } else if (st.off_track) {
        s_delta_sprite.drawString("OFF TRACK",
                                  SCREEN_W / 2, DELTA_AREA_H / 2, 4);
    } else if (!st.delta_valid) {
        s_delta_sprite.drawString("---",
                                  SCREEN_W / 2, DELTA_AREA_H / 2, 7);
    } else {
        char delta_buf[10];
        format_delta(delta_buf, sizeof(delta_buf), st.delta_ms);
        s_delta_sprite.drawString(delta_buf,
                                  SCREEN_W / 2, DELTA_AREA_H / 2, 7);
    }

    s_delta_sprite.pushSprite(0, DELTA_AREA_Y);
}

static void draw_driving_bottom_bar(const SessionState& st) {
    int y = SCREEN_H - BOTTOM_BAR_H;
    uint16_t bg = delta_background_colour(st);

    s_tft.fillRect(0, y, SCREEN_W, BOTTOM_BAR_H, bg);

    s_tft.setTextDatum(BL_DATUM);
    s_tft.setTextColor(TFT_WHITE, bg);

    char best_buf[20];
    if (st.best_lap_time_ms >= 0) {
        char t[12];
        format_lap_time(t, sizeof(t), st.best_lap_time_ms);
        snprintf(best_buf, sizeof(best_buf), "Best:%s", t);
    } else {
        snprintf(best_buf, sizeof(best_buf), "Best:--:--.--");
    }
    s_tft.drawString(best_buf, 4, SCREEN_H - 2, 1);

    s_tft.setTextDatum(BR_DATUM);
    char gps_buf[16];
    const char* fix_icon = st.gps_fix_ok ? "*" : "?";
    snprintf(gps_buf, sizeof(gps_buf), "%s%d sats",
             fix_icon, st.gps_satellites);
    s_tft.drawString(gps_buf, SCREEN_W - 4, SCREEN_H - 2, 1);
}

static void draw_driving_screen(const DirtyFlags& df,
                                const SessionState& st) {
    if (df.full_redraw || df.background) {
        uint16_t bg = delta_background_colour(st);
        s_tft.fillRect(0, 0, SCREEN_W, INFO_BAR_H, bg);
    }

    if (df.full_redraw || df.lap_number || df.background) {
        draw_driving_top_bar(st);
    }
    if (df.full_redraw || df.current_time || df.background) {
        draw_driving_current_time(st);
    }
    if (df.full_redraw || df.delta || df.background) {
        draw_driving_delta(st);
    }
    if (df.full_redraw || df.best_time || df.gps_info || df.background) {
        draw_driving_bottom_bar(st);
    }
}

// ============================================================
// Status screen rendering
// ============================================================

static void draw_status_screen(const SessionState& st) {
    s_tft.fillScreen(TFT_BLACK);
    s_tft.setTextDatum(TL_DATUM);
    s_tft.setTextColor(TFT_WHITE, TFT_BLACK);

    int y = 8;
    constexpr int LINE_H = 28;

    // GPS
    char gps_line[40];
    snprintf(gps_line, sizeof(gps_line), "GPS: %d sats  Fix: %s",
             st.gps_satellites,
             st.gps_fix_ok ? "3D" : "No fix");
    s_tft.drawString(gps_line, 8, y, 2);
    y += LINE_H;

    // Track
    char track_line[80];
    if (st.track_name[0] != '\0') {
        snprintf(track_line, sizeof(track_line), "Track: %s", st.track_name);
    } else {
        snprintf(track_line, sizeof(track_line), "Track: No track");
    }
    s_tft.drawString(track_line, 8, y, 2);
    y += LINE_H;

    // Recording
    const char* rec_str = st.is_recording ? "REC" : "Idle";
    char rec_line[32];
    snprintf(rec_line, sizeof(rec_line), "Recording: %s", rec_str);
    s_tft.setTextColor(st.is_recording ? TFT_RED : TFT_WHITE, TFT_BLACK);
    s_tft.drawString(rec_line, 8, y, 2);
    y += LINE_H;
    s_tft.setTextColor(TFT_WHITE, TFT_BLACK);

    // Laps
    char lap_line[32];
    snprintf(lap_line, sizeof(lap_line), "Laps: %d completed", st.lap_count);
    s_tft.drawString(lap_line, 8, y, 2);
    y += LINE_H;

    // WiFi SSID from config
    char wifi_line[48];
    snprintf(wifi_line, sizeof(wifi_line), "WiFi: %s", app_config.wifi_ssid);
    s_tft.drawString(wifi_line, 8, y, 2);
    y += LINE_H;

    // SD free space (from cached value, updated outside render lock)
    char sd_line[32];
    if (s_sd_free_gb >= 0.0f) {
        snprintf(sd_line, sizeof(sd_line), "SD: %.1f GB free", (double)s_sd_free_gb);
    } else {
        snprintf(sd_line, sizeof(sd_line), "SD: --");
    }
    s_tft.drawString(sd_line, 8, y, 2);
    y += LINE_H;

    // Uptime
    uint32_t up_s = millis() / 1000;
    char up_line[32];
    snprintf(up_line, sizeof(up_line), "Uptime: %lum %lus",
             (unsigned long)(up_s / 60), (unsigned long)(up_s % 60));
    s_tft.drawString(up_line, 8, y, 2);
    y += LINE_H;

    // Firmware
    char fw_line[24];
    snprintf(fw_line, sizeof(fw_line), "FW: %s", FW_VERSION);
    s_tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    s_tft.drawString(fw_line, 8, y, 2);
}

// ============================================================
// Lap list screen rendering
// ============================================================

static void draw_lap_list_header(const SessionState& st) {
    s_tft.fillRect(0, 0, SCREEN_W, LAP_HEADER_H, TFT_NAVY);
    s_tft.setTextDatum(TL_DATUM);
    s_tft.setTextColor(TFT_WHITE, TFT_NAVY);

    char hdr[40];
    snprintf(hdr, sizeof(hdr), "SESSION: %d laps", st.lap_count);
    s_tft.drawString(hdr, 8, 6, 2);
}

static void draw_lap_list_row(int row_idx, const LapRecord& lap,
                              int32_t best_time_ms) {
    int y = LAP_HEADER_H + row_idx * LAP_ROW_H;
    bool is_best = (lap.lap_time_ms == best_time_ms &&
                    lap.status == LAP_STATUS_TIMED);

    uint16_t bg = is_best ? TFT_DARKGREEN : TFT_BLACK;
    s_tft.fillRect(0, y, SCREEN_W, LAP_ROW_H, bg);

    uint16_t fg = lap_status_colour(lap.status);
    s_tft.setTextColor(fg, bg);
    s_tft.setTextDatum(TL_DATUM);

    // Lap number
    char num_buf[6];
    snprintf(num_buf, sizeof(num_buf), "%3d", lap.lap_number);
    s_tft.drawString(num_buf, 8, y + 6, 2);

    // Lap time
    char time_buf[12];
    format_lap_time(time_buf, sizeof(time_buf), lap.lap_time_ms);
    s_tft.drawString(time_buf, 60, y + 6, 2);

    // Delta vs best
    s_tft.setTextDatum(TR_DATUM);
    if (is_best) {
        s_tft.setTextColor(TFT_GREEN, bg);
        s_tft.drawString("BEST", SCREEN_W - 8, y + 6, 2);
    } else if (best_time_ms > 0 && lap.status == LAP_STATUS_TIMED) {
        int32_t d = lap.lap_time_ms - best_time_ms;
        char delta_buf[10];
        format_delta(delta_buf, sizeof(delta_buf), d);
        s_tft.drawString(delta_buf, SCREEN_W - 8, y + 6, 2);
    }
}

static void draw_lap_list_screen(const SessionState& st) {
    s_tft.fillScreen(TFT_BLACK);
    draw_lap_list_header(st);

    if (st.lap_count == 0) {
        s_tft.setTextDatum(MC_DATUM);
        s_tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        s_tft.drawString("No laps yet", SCREEN_W / 2, SCREEN_H / 2, 2);
        return;
    }

    // Clamp scroll offset
    int max_scroll = (st.lap_count > LAPS_PER_PAGE)
                     ? (st.lap_count - LAPS_PER_PAGE)
                     : 0;
    if (s_lap_list_scroll > max_scroll) {
        s_lap_list_scroll = max_scroll;
    }

    int rows_to_draw = (st.lap_count - s_lap_list_scroll < LAPS_PER_PAGE)
                       ? (st.lap_count - s_lap_list_scroll)
                       : LAPS_PER_PAGE;

    for (int i = 0; i < rows_to_draw; i++) {
        int lap_idx = s_lap_list_scroll + i;
        if (lap_idx < st.lap_count) {
            draw_lap_list_row(i, st.laps[lap_idx], st.best_lap_time_ms);
        }
    }
}

// ============================================================
// SPI-guarded draw dispatch
// ============================================================

static void render_frame(bool screen_changed) {
    DirtyFlags df = compute_dirty(s_cached_state, s_prev_state,
                                  screen_changed);

    // Acquire SPI bus with 10ms timeout; skip frame on failure
    if (xSemaphoreTake(s_spi_mtx, pdMS_TO_TICKS(SPI_TIMEOUT_MS)) != pdTRUE) {
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

static void init_tft() {
    s_tft.init();
    s_tft.setRotation(1);  // landscape 320x240
    s_tft.fillScreen(TFT_BLACK);
}

static void init_delta_sprite() {
    s_delta_sprite.createSprite(SCREEN_W, DELTA_AREA_H);
    s_delta_sprite.setTextDatum(MC_DATUM);
}

// ============================================================
// Public API
// ============================================================

void display_init(QueueHandle_t     btn_display_q,
                  SemaphoreHandle_t spi_mtx,
                  SemaphoreHandle_t session_mtx) {
    s_btn_queue   = btn_display_q;
    s_spi_mtx     = spi_mtx;
    s_session_mtx = session_mtx;

    init_backlight();
    init_tft();
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
// Boot sequence screens (called before display_task starts)
// Draw directly to TFT — no sprite, no mutex needed.
// ============================================================

void display_show_splash() {
    s_tft.fillScreen(TFT_BLACK);
    s_tft.setTextDatum(MC_DATUM);
    s_tft.setTextColor(TFT_WHITE, TFT_BLACK);
    s_tft.drawString("GPS Lap Timer", SCREEN_W / 2, SCREEN_H / 2 - 20, 4);
    s_tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    s_tft.drawString(FW_VERSION, SCREEN_W / 2, SCREEN_H / 2 + 20, 2);
}

void display_show_gps_search(int sats) {
    s_tft.fillScreen(TFT_BLACK);
    s_tft.setTextDatum(MC_DATUM);
    s_tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    s_tft.drawString("GPS Searching...", SCREEN_W / 2, SCREEN_H / 2 - 20, 4);

    char sat_buf[24];
    snprintf(sat_buf, sizeof(sat_buf), "%d satellites", sats);
    s_tft.setTextColor(TFT_WHITE, TFT_BLACK);
    s_tft.drawString(sat_buf, SCREEN_W / 2, SCREEN_H / 2 + 20, 2);
}

void display_show_track_found(const char* name) {
    s_tft.fillScreen(TFT_BLACK);
    s_tft.setTextDatum(MC_DATUM);
    s_tft.setTextColor(TFT_CYAN, TFT_BLACK);
    s_tft.drawString("Track:", SCREEN_W / 2, SCREEN_H / 2 - 20, 2);
    s_tft.setTextColor(TFT_WHITE, TFT_BLACK);
    s_tft.drawString(name, SCREEN_W / 2, SCREEN_H / 2 + 10, 4);
}

void display_show_ready() {
    s_tft.fillScreen(TFT_BLACK);
    s_tft.setTextDatum(MC_DATUM);
    s_tft.setTextColor(TFT_GREEN, TFT_BLACK);
    s_tft.drawString("READY", SCREEN_W / 2, SCREEN_H / 2, 7);
}

// ============================================================
// Display task (FreeRTOS)
// ============================================================

void display_task(void* param) {
    (void)param;

    TickType_t last_wake = xTaskGetTickCount();

    // Force full redraw on first frame
    bool first_frame = true;

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FRAME_INTERVAL_MS));

        // 1. Snapshot session state (skip frame if mutex busy)
        if (!snapshot_session_state()) {
            continue;
        }

        // 2. Handle button input, determine if screen changed
        bool screen_changed = handle_button_events() || first_frame;
        first_frame = false;

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
        render_frame(screen_changed);
    }
}
