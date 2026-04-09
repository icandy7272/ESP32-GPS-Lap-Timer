// ============================================================
// Lap list screen rendering
// ============================================================

#include "display_internal.h"
#include "types.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <stdio.h>

static void draw_lap_list_header(const SessionState& st) {
    if (!s_laplist_header_drawn) {
        s_tft.fillRect(0, 0, SCREEN_W, LAP_HEADER_H, TFT_NAVY);
        s_laplist_header_drawn = true;
    }
    s_tft.setTextDatum(TL_DATUM);
    s_tft.setTextColor(TFT_WHITE, TFT_NAVY);

    char hdr[40];
    snprintf(hdr, sizeof(hdr), "SESSION: %d laps   ", st.lap_count);
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

// Track empty-state visibility so we can clear it when first lap appears.
static bool s_laplist_empty_drawn = false;

void draw_lap_list_screen(const SessionState& st) {
    if (s_laplist_needs_clear) {
        s_tft.fillScreen(TFT_BLACK);
        s_laplist_needs_clear = false;
        s_laplist_empty_drawn = false;
    }
    draw_lap_list_header(st);

    if (st.lap_count == 0) {
        if (!s_laplist_empty_drawn) {
            s_tft.setTextDatum(MC_DATUM);
            s_tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
            s_tft.drawString("No laps yet", SCREEN_W / 2, SCREEN_H / 2, 2);
            s_laplist_empty_drawn = true;
        }
        return;
    }

    // Transitioning from empty to non-empty: clear the leftover "No laps yet"
    if (s_laplist_empty_drawn) {
        // Clear the rows area below header (leaves header intact)
        s_tft.fillRect(0, LAP_HEADER_H, SCREEN_W, SCREEN_H - LAP_HEADER_H, TFT_BLACK);
        s_laplist_empty_drawn = false;
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
