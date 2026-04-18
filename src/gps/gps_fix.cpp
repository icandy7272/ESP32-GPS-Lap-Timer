#include "gps/gps_internal.h"

#include <esp_timer.h>

namespace {

static constexpr float GPS_DIAG_LOW_SPEED_KMH = 5.0f;

}

static bool is_pps_fresh() {
    int64_t now = esp_timer_get_time();
    int64_t last_pps = pps_read();
    return (last_pps > 0) && ((now - last_pps) < PPS_STALE_US);
}

static int64_t compute_timestamp(int fix_index) {
    int64_t base = pps_read();
    return base + (static_cast<int64_t>(fix_index) * FIX_INTERVAL_US);
}

static GpsPoint assemble_point(const GgaData* gga, const RmcData* rmc) {
    bool pps_ok = is_pps_fresh();

    GpsPoint point = {};
    point.lat_deg      = gga->lat_deg;
    point.lon_deg      = gga->lon_deg;
    point.height_m     = gga->height_m;
    point.satellites   = gga->satellites;
    point.speed_kmh    = rmc->speed_kmh;
    point.heading_deg  = rmc->heading_deg;

    int fq = gga->fix_quality;
    point.fix_3d     = (fq == 1 || fq == 2 || fq == 4 || fq == 5);
    point.pps_synced = pps_ok;

    if (pps_ok) {
        point.timestamp_us = compute_timestamp(s_fix_idx);
    } else {
        point.timestamp_us = esp_timer_get_time();
    }

    return point;
}

void gps_send_fix_if_ready() {
    if (!s_gga.valid || !s_rmc.valid) {
        return;
    }

    int64_t current_pps = pps_read();
    if (current_pps != s_last_pps_seen) {
        s_last_pps_seen = current_pps;
        s_fix_idx = 0;
    }

    GpsPoint point = assemble_point(&s_gga, &s_rmc);

    bool queue_was_full = false;
    if (xQueueSend(s_gps_queue, &point, 0) == errQUEUE_FULL) {
        queue_was_full = true;
        GpsPoint discard;
        xQueueReceive(s_gps_queue, &discard, 0);
        xQueueSend(s_gps_queue, &point, 0);
    }

    static uint32_t last_diag_ms = 0;
    static uint16_t accepted_since_last = 0;
    static uint16_t drops_since_last = 0;
    static uint16_t low_speed_since_last = 0;
    static int min_sats_since_last = 99;
    static int max_sats_since_last = 0;

    accepted_since_last++;
    if (queue_was_full) {
        drops_since_last++;
    }
    if (point.speed_kmh < GPS_DIAG_LOW_SPEED_KMH) {
        low_speed_since_last++;
    }
    if (point.satellites < min_sats_since_last) {
        min_sats_since_last = point.satellites;
    }
    if (point.satellites > max_sats_since_last) {
        max_sats_since_last = point.satellites;
    }

    uint32_t now_ms = millis();
    if (now_ms - last_diag_ms >= 1000) {
        int min_sats = (accepted_since_last > 0) ? min_sats_since_last : 0;
        int max_sats = (accepted_since_last > 0) ? max_sats_since_last : 0;
        Serial.printf("[gps] accepted=%u/s drops=%u sats=%d-%d low=%u "
                      "fix_q=%d fix_3d=%d pps=%d lat=%.5f lon=%.5f\n",
                      accepted_since_last, drops_since_last,
                      min_sats, max_sats, low_speed_since_last,
                      s_gga.fix_quality,
                      point.fix_3d ? 1 : 0, point.pps_synced ? 1 : 0,
                      point.lat_deg, point.lon_deg);
        last_diag_ms = now_ms;
        accepted_since_last = 0;
        drops_since_last = 0;
        low_speed_since_last = 0;
        min_sats_since_last = 99;
        max_sats_since_last = 0;
    }

    s_fix_idx++;
    if (s_fix_idx >= GPS_FIX_RATE_HZ) {
        s_fix_idx = 0;
    }

    s_gga.valid = false;
    s_rmc.valid = false;
}
