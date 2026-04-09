#include "gps/gps_internal.h"

// Shared GPS state lives here so the split implementation files can stay
// focused on parsing, fix assembly, UART config, and task entrypoints.

volatile int64_t pps_sync_us = 0;
portMUX_TYPE pps_mux = portMUX_INITIALIZER_UNLOCKED;

QueueHandle_t s_gps_queue = nullptr;

int     GPS_FIX_RATE_HZ = 25;
int64_t FIX_INTERVAL_US = 40000;

GgaData s_gga = {};
RmcData s_rmc = {};
bool    s_rtc_synced = false;
int     s_fix_idx = 0;
int64_t s_last_pps_seen = 0;

char s_nmea_buf[NMEA_MAX_LEN + 1];
int  s_nmea_len = 0;
bool s_nmea_receiving = false;

int64_t pps_read() {
    portENTER_CRITICAL(&pps_mux);
    int64_t val = pps_sync_us;
    portEXIT_CRITICAL(&pps_mux);
    return val;
}
