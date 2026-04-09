// ============================================================
// Status screen rendering
// ============================================================

#include "display_internal.h"
#include "config.h"
#include "types.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <stdio.h>

void draw_status_screen(const SessionState& st) {
    if (s_status_needs_clear) {
        s_tft.fillScreen(TFT_BLACK);
        s_status_needs_clear = false;
    }
    s_tft.setTextDatum(TL_DATUM);
    s_tft.setTextColor(TFT_WHITE, TFT_BLACK);

    int y = 4;
    constexpr int LINE_H = 25;

    // GPS
    char gps_line[40];
    snprintf(gps_line, sizeof(gps_line), "GPS: %d sats  Fix: %s",
             st.gps_satellites,
             st.gps_fix_ok ? "OK" : "No fix");
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

    // Battery (no IC in v1.0)
    s_tft.drawString("Battery: N/A", 8, y, 2);
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

    // Clear any leftover area below last line
    s_tft.fillRect(0, y + LINE_H, SCREEN_W, SCREEN_H - y - LINE_H, TFT_BLACK);
}
