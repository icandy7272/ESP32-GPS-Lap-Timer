// ============================================================
// Boot sequence screens (called before display_task starts)
// Draw directly to TFT — no sprite, no mutex needed.
// ============================================================

#include "display.h"
#include "display_internal.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <stdio.h>

void display_show_splash() {
    s_tft.fillScreen(TFT_BLACK);
    s_tft.setTextDatum(MC_DATUM);
    s_tft.setTextColor(TFT_WHITE, TFT_BLACK);
    s_tft.drawString("GPS Lap Timer", SCREEN_W / 2, SCREEN_H / 2 - 20, 4);
    s_tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    s_tft.drawString(FW_VERSION, SCREEN_W / 2, SCREEN_H / 2 + 20, 2);
}

static bool s_gps_search_cleared = false;

void display_show_gps_search(int sats) {
    if (!s_gps_search_cleared) {
        s_tft.fillScreen(TFT_BLACK);
        s_tft.setTextDatum(MC_DATUM);
        s_tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        s_tft.drawString("GPS Searching...", SCREEN_W / 2, SCREEN_H / 2 - 20, 4);
        s_gps_search_cleared = true;
    }

    // Only update the satellite count (overwrite with background color)
    char sat_buf[24];
    snprintf(sat_buf, sizeof(sat_buf), "%d satellites  ", sats);  // trailing spaces to clear
    s_tft.setTextDatum(MC_DATUM);
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

void display_show_recovery() {
    s_tft.fillScreen(TFT_BLACK);
    s_tft.setTextDatum(MC_DATUM);
    s_tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    s_tft.drawString("Session Recovered", SCREEN_W / 2, SCREEN_H / 2, 4);
}

void display_show_ready() {
    s_tft.fillScreen(TFT_BLACK);
    s_tft.setTextDatum(MC_DATUM);
    s_tft.setTextColor(TFT_GREEN, TFT_BLACK);
    s_tft.drawString("READY", SCREEN_W / 2, SCREEN_H / 2, 7);
}
