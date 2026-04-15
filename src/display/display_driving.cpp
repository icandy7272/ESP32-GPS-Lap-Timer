// ============================================================
// Driving screen rendering
//
// Three display states:
//   1. IDLE     — not recording. Center shows big satellite count
//                 + 3D FIX / NO FIX indicator.
//   2. OUT_LAP  — recording but first start-line crossing has not
//                 happened yet (st.current_lap == 0). Center shows
//                 current speed + "OUT LAP" label.
//   3. NORMAL   — recording and at least one crossing has occurred.
//                 Center shows delta vs best lap (original behavior).
// ============================================================

#include "display_internal.h"
#include "types.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_timer.h>
#include <stdio.h>

// --- Top bar -----------------------------------------------------------

static void draw_driving_top_bar(const SessionState& st) {
    s_tft.setTextDatum(TL_DATUM);
    s_tft.setTextColor(TFT_WHITE, delta_background_colour(st));

    // Clear the top-left label region so old text of different widths
    // (e.g. "OUT LAP" → "L3") does not leave ghosts.
    s_tft.fillRect(0, 0, SCREEN_W / 2, INFO_BAR_H, delta_background_colour(st));

    char label[16];
    switch (get_driving_state(st)) {
        case DRIVING_IDLE:
            snprintf(label, sizeof(label), "READY");
            break;
        case DRIVING_OUT_LAP:
            snprintf(label, sizeof(label), "OUT LAP");
            break;
        case DRIVING_NORMAL:
        default:
            snprintf(label, sizeof(label), "L%d", st.current_lap);
            break;
    }
    s_tft.drawString(label, 4, 4, 2);
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

// --- Center area (per-state) -------------------------------------------
//
// Sprite dimensions: SCREEN_W × DELTA_AREA_H (320 × 196).
//
// Layout rows (y positions within sprite):
//   row1  y = 50   — large value (font 7, 48 px, digits/symbols only)
//   row2  y = 110  — unit/label (font 4, 26 px, full ASCII)
//   row3  y = 155  — status (font 4, 26 px)
//
// Font 7 is a 7-segment numeric font — use it only for pure numeric
// content. Font 4 has the full ASCII set.

static constexpr int ROW1_Y = 50;
static constexpr int ROW2_Y = 110;
static constexpr int ROW3_Y = 155;

static void draw_delta_idle(const SessionState& st) {
    // Row1 big sats, row2 "SATS" label, row3 track name.
    // Fix quality is still visible in the bottom bar via the '*' vs '?'
    // prefix, so dropping "3D FIX" from the center frees room for the
    // track name, which is what the driver actually needs at READY.
    char num_buf[8];
    snprintf(num_buf, sizeof(num_buf), "%d", st.gps_satellites);
    s_delta_sprite.drawString(num_buf, SCREEN_W / 2, ROW1_Y, 7);
    s_delta_sprite.drawString("SATS", SCREEN_W / 2, ROW2_Y, 4);

    const char* track_str = st.track_name[0] != '\0'
                                ? st.track_name
                                : "No Track";
    s_delta_sprite.drawString(track_str, SCREEN_W / 2, ROW3_Y, 4);
}

static void draw_delta_out_lap(const SessionState& st) {
    // Big speed (km/h) + unit label + "OUT LAP" indicator.
    int speed = (int)(st.speed_kmh + 0.5f);
    if (speed < 0) speed = 0;
    char num_buf[8];
    snprintf(num_buf, sizeof(num_buf), "%d", speed);
    s_delta_sprite.drawString(num_buf, SCREEN_W / 2, ROW1_Y, 7);
    s_delta_sprite.drawString("km/h", SCREEN_W / 2, ROW2_Y, 4);
    s_delta_sprite.drawString("OUT LAP", SCREEN_W / 2, ROW3_Y, 4);
}

static void draw_delta_normal(const SessionState& st) {
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
}

static void draw_driving_delta(const SessionState& st) {
    uint16_t bg = delta_background_colour(st);

    s_delta_sprite.fillSprite(bg);
    s_delta_sprite.setTextDatum(MC_DATUM);
    s_delta_sprite.setTextColor(TFT_WHITE, bg);

    switch (get_driving_state(st)) {
        case DRIVING_IDLE:
            draw_delta_idle(st);
            break;
        case DRIVING_OUT_LAP:
            draw_delta_out_lap(st);
            break;
        case DRIVING_NORMAL:
        default:
            draw_delta_normal(st);
            break;
    }

    s_delta_sprite.pushSprite(0, DELTA_AREA_Y);
}

// --- Bottom bar --------------------------------------------------------

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

// --- Entry point -------------------------------------------------------

void draw_driving_screen(const DirtyFlags& df,
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
