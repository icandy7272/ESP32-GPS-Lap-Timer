#include "gps/gps_internal.h"

#include <esp_timer.h>

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
    static uint16_t fixes_since_last = 0;
    static uint16_t drops_since_last = 0;
    fixes_since_last++;
    if (queue_was_full) {
        drops_since_last++;
    }

    uint32_t now_ms = millis();
    if (now_ms - last_diag_ms >= 1000) {
        Serial.printf("[gps] %u/s drops=%u fix_q=%d sats=%d "
                      "fix_3d=%d pps=%d lat=%.5f lon=%.5f\n",
                      fixes_since_last, drops_since_last,
                      s_gga.fix_quality, s_gga.satellites,
                      point.fix_3d ? 1 : 0, point.pps_synced ? 1 : 0,
                      point.lat_deg, point.lon_deg);
        last_diag_ms = now_ms;
        fixes_since_last = 0;
        drops_since_last = 0;
    }

    s_fix_idx++;
    if (s_fix_idx >= GPS_FIX_RATE_HZ) {
        s_fix_idx = 0;
    }

    s_gga.valid = false;
    s_rmc.valid = false;
}
