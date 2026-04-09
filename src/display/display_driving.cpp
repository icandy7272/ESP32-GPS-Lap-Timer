// ============================================================
// Driving screen rendering
// ============================================================

#include "display_internal.h"
#include "types.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_timer.h>
#include <stdio.h>

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
