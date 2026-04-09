#include "display_internal.h"
#include "types.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================
// Colour helpers
// ============================================================

uint16_t delta_background_colour(const SessionState& st) {
    if (!st.gps_fix_ok)  return TFT_DARKGREY;
    if (st.off_track)    return TFT_YELLOW;
    if (!st.delta_valid) return TFT_DARKGREY;
    if (st.delta_ms < 0) return TFT_GREEN;
    if (st.delta_ms > 0) return TFT_RED;
    return TFT_DARKGREY;
}

uint16_t lap_status_colour(uint8_t status) {
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
void format_lap_time(char* buf, size_t len, int32_t time_ms) {
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
void format_delta(char* buf, size_t len, int32_t delta_ms) {
    const char sign = (delta_ms >= 0) ? '+' : '-';
    int abs_ms = abs(delta_ms);
    int secs   = abs_ms / 1000;
    int hundr  = (abs_ms % 1000) / 10;
    snprintf(buf, len, "%c%d.%02d", sign, secs, hundr);
}
