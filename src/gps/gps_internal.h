#pragma once

#include "gps.h"
#include "types.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdint.h>

static constexpr int NMEA_MAX_LEN = 120;
static constexpr int MAX_FIELDS = 20;
static constexpr int PPS_STALE_US = 1100000;

struct GgaData {
    double lat_deg;
    double lon_deg;
    float  height_m;
    int    satellites;
    int    fix_quality;
    bool   valid;
};

struct RmcData {
    float speed_kmh;
    float heading_deg;
    bool  valid;
};

struct GsaData {
    int   fix_type;
    float pdop;
    float hdop;
    float vdop;
    bool  valid;
};

extern volatile int64_t pps_sync_us;
extern portMUX_TYPE pps_mux;

extern QueueHandle_t s_gps_queue;

extern int     GPS_FIX_RATE_HZ;
extern int64_t FIX_INTERVAL_US;

extern GgaData s_gga;
extern RmcData s_rmc;
extern GsaData s_gsa;
extern bool    s_rtc_synced;
extern int     s_fix_idx;
extern int64_t s_last_pps_seen;

extern char s_nmea_buf[NMEA_MAX_LEN + 1];
extern int  s_nmea_len;
extern bool s_nmea_receiving;

void gps_feed_char(char c);
void gps_send_fix_if_ready();
void gps_uart_init();
