// ============================================================
// GPS Module — ESP32-S3 GPS Lap Timer
// UART2 NMEA reception, PPS hardware interrupt, hand-rolled
// NMEA parser (GGA + RMC), PPS-corrected timestamping.
// ============================================================

#include "gps.h"
#include "pins.h"
#include "types.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// ---- Constants ------------------------------------------------

static constexpr int    NMEA_MAX_LEN       = 120;   // max NMEA sentence length
static constexpr int    GPS_FIX_RATE_HZ    = 25;
static constexpr int    PPS_STALE_US       = 1100000; // 1100 ms — PPS considered stale
static constexpr int    UART_READ_TIMEOUT  = 10;      // ms, per-character timeout
static constexpr int    WDT_TIMEOUT_SEC    = 5;
static constexpr int    FIX_INTERVAL_US    = 40000;   // 1e6 / 25 Hz

// ---- Shared state ---------------------------------------------

static volatile int64_t pps_sync_us = 0;
static portMUX_TYPE pps_mux = portMUX_INITIALIZER_UNLOCKED;

int64_t pps_read() {
    portENTER_CRITICAL(&pps_mux);
    int64_t val = pps_sync_us;
    portEXIT_CRITICAL(&pps_mux);
    return val;
}

// ---- Module-private state -------------------------------------

static QueueHandle_t s_gps_queue = nullptr;

// Partial GGA data — accumulated until RMC completes the fix.
struct GgaData {
    double lat_deg;
    double lon_deg;
    float  height_m;
    int    satellites;
    int    fix_quality;    // 0 = no fix, 1 = GPS, 2 = DGPS, etc.
    bool   valid;
};

// Partial RMC data.
struct RmcData {
    float  speed_kmh;
    float  heading_deg;
    bool   valid;
};

static GgaData  s_gga          = {};
static RmcData  s_rmc          = {};
static int      s_fix_idx      = 0;   // 0..24 within the current PPS second
static int64_t  s_last_pps_seen = 0;  // tracks when we last noticed a PPS change

// NMEA sentence accumulator.
static char s_nmea_buf[NMEA_MAX_LEN + 1];
static int  s_nmea_len = 0;
static bool s_nmea_receiving = false;

// ---- PPS ISR --------------------------------------------------

static void IRAM_ATTR pps_isr() {
    portENTER_CRITICAL_ISR(&pps_mux);
    pps_sync_us = esp_timer_get_time();
    portEXIT_CRITICAL_ISR(&pps_mux);
}

// ---- NMEA helpers ---------------------------------------------

// Verify NMEA checksum: XOR of all chars between '$' and '*'.
static bool nmea_checksum_valid(const char* sentence, int len) {
    if (len < 4 || sentence[0] != '$') {
        return false;
    }

    uint8_t computed = 0;
    int star_pos = -1;

    for (int i = 1; i < len; i++) {
        if (sentence[i] == '*') {
            star_pos = i;
            break;
        }
        computed ^= static_cast<uint8_t>(sentence[i]);
    }

    if (star_pos < 0 || star_pos + 2 >= len) {
        return false;
    }

    char hex[3] = { sentence[star_pos + 1], sentence[star_pos + 2], '\0' };
    uint8_t expected = static_cast<uint8_t>(strtoul(hex, nullptr, 16));
    return computed == expected;
}

// Convert NMEA latitude/longitude (ddmm.mmmmm or dddmm.mmmmm) to
// decimal degrees. Returns positive value; caller applies sign.
static double nmea_coord_to_deg(const char* field, int deg_digits) {
    if (field[0] == '\0') {
        return 0.0;
    }
    char deg_str[4] = {};
    for (int i = 0; i < deg_digits && i < 3; i++) {
        deg_str[i] = field[i];
    }
    double degrees = atof(deg_str);
    double minutes = atof(field + deg_digits);
    return degrees + (minutes / 60.0);
}

// Apply N/S or E/W sign.
static double apply_hemisphere(double value, char dir) {
    if (dir == 'S' || dir == 'W') {
        return -value;
    }
    return value;
}

// ---- Comma-field tokeniser ------------------------------------
// Splits an NMEA sentence body (between '$...,' and '*') into fields.
// Returns the number of fields extracted.

static constexpr int MAX_FIELDS = 20;

static int nmea_split_fields(char* body, char* fields[], int max_fields) {
    int count = 0;
    char* ptr = body;

    while (ptr != nullptr && count < max_fields) {
        fields[count++] = ptr;
        char* comma = strchr(ptr, ',');
        if (comma != nullptr) {
            *comma = '\0';
            ptr = comma + 1;
        } else {
            break;
        }
    }
    return count;
}

// ---- GGA parser -----------------------------------------------
// $GNGGA,hhmmss.ss,lat,N,lon,E,quality,sats,hdop,alt,M,geoid,M,...*cs
// Fields (0-indexed after sentence ID):
//   0=time, 1=lat, 2=N/S, 3=lon, 4=E/W, 5=quality, 6=sats,
//   7=hdop, 8=alt, 9=M, 10=geoid, 11=M

static GgaData parse_gga(char* body) {
    GgaData result = {};
    char* fields[MAX_FIELDS] = {};
    int count = nmea_split_fields(body, fields, MAX_FIELDS);

    if (count < 10) {
        return result;
    }

    result.fix_quality = atoi(fields[5]);
    if (result.fix_quality == 0) {
        return result;
    }

    result.lat_deg    = apply_hemisphere(nmea_coord_to_deg(fields[1], 2), fields[2][0]);
    result.lon_deg    = apply_hemisphere(nmea_coord_to_deg(fields[3], 3), fields[4][0]);
    result.satellites = atoi(fields[6]);
    result.height_m   = static_cast<float>(atof(fields[8]));
    result.valid      = true;
    return result;
}

// ---- RMC parser -----------------------------------------------
// $GNRMC,hhmmss.ss,status,lat,N,lon,E,speed_kt,heading,date,...*cs
// Fields (0-indexed after sentence ID):
//   0=time, 1=status, 2=lat, 3=N/S, 4=lon, 5=E/W, 6=speed_kn,
//   7=heading, 8=date

static RmcData parse_rmc(char* body) {
    RmcData result = {};
    char* fields[MAX_FIELDS] = {};
    int count = nmea_split_fields(body, fields, MAX_FIELDS);

    if (count < 8) {
        return result;
    }

    if (fields[1][0] != 'A') {  // 'A' = active, 'V' = void
        return result;
    }

    double speed_knots = atof(fields[6]);
    result.speed_kmh   = static_cast<float>(speed_knots * 1.852);
    result.heading_deg = (fields[7][0] != '\0') ? static_cast<float>(atof(fields[7])) : 0.0f;
    result.valid       = true;
    return result;
}

// ---- Sentence type detection ----------------------------------

enum class NmeaType {
    GGA,
    RMC,
    UNKNOWN
};

// Match $GNGGA, $GPGGA, $GNRMC, $GPRMC.
static NmeaType detect_sentence_type(const char* sentence) {
    if (strncmp(sentence + 3, "GGA", 3) == 0) {
        return NmeaType::GGA;
    }
    if (strncmp(sentence + 3, "RMC", 3) == 0) {
        return NmeaType::RMC;
    }
    return NmeaType::UNKNOWN;
}

// Check for $GN or $GP prefix.
static bool has_valid_prefix(const char* sentence) {
    if (sentence[0] != '$') {
        return false;
    }
    bool gn = (sentence[1] == 'G' && sentence[2] == 'N');
    bool gp = (sentence[1] == 'G' && sentence[2] == 'P');
    return gn || gp;
}

// ---- GpsPoint assembly ----------------------------------------

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
    point.fix_3d       = (gga->satellites >= 6);
    point.pps_synced   = pps_ok;

    if (pps_ok) {
        point.timestamp_us = compute_timestamp(s_fix_idx);
    } else {
        point.timestamp_us = esp_timer_get_time();
    }

    return point;
}

// ---- Forward declarations -------------------------------------

static void send_fix_if_ready();

// ---- Sentence dispatch ----------------------------------------

static void process_sentence(char* sentence, int len) {
    if (!nmea_checksum_valid(sentence, len)) {
        return;
    }
    if (!has_valid_prefix(sentence)) {
        return;
    }

    NmeaType type = detect_sentence_type(sentence);
    if (type == NmeaType::UNKNOWN) {
        return;
    }

    // Find the first comma after the sentence ID to get the body.
    char* body = strchr(sentence + 1, ',');
    if (body == nullptr) {
        return;
    }
    body++;  // skip the comma

    // Truncate at '*' so the tokeniser does not include the checksum.
    char* star = strchr(body, '*');
    if (star != nullptr) {
        *star = '\0';
    }

    if (type == NmeaType::GGA) {
        s_gga = parse_gga(body);
    } else if (type == NmeaType::RMC) {
        s_rmc = parse_rmc(body);
        send_fix_if_ready();
    }
}

// ---- Fix transmission -----------------------------------------

static void send_fix_if_ready() {
    if (!s_gga.valid || !s_rmc.valid) {
        return;
    }

    // Detect new PPS pulse — reset fix index to 0 for new second.
    int64_t current_pps = pps_read();
    if (current_pps != s_last_pps_seen) {
        s_last_pps_seen = current_pps;
        s_fix_idx = 0;
    }

    GpsPoint point = assemble_point(&s_gga, &s_rmc);

    // Send to queue; if full, discard oldest item then retry.
    if (xQueueSend(s_gps_queue, &point, 0) == errQUEUE_FULL) {
        GpsPoint discard;
        xQueueReceive(s_gps_queue, &discard, 0);
        xQueueSend(s_gps_queue, &point, 0);
    }

    // Advance fix index within the current PPS second (0..24).
    s_fix_idx++;
    if (s_fix_idx >= GPS_FIX_RATE_HZ) {
        s_fix_idx = 0;
    }

    // Invalidate partials so we require fresh GGA+RMC for next fix.
    s_gga.valid = false;
    s_rmc.valid = false;
}

// ---- Character-level NMEA accumulator -------------------------

static void feed_char(char c) {
    if (c == '$') {
        // Start of a new sentence.
        s_nmea_buf[0] = '$';
        s_nmea_len = 1;
        s_nmea_receiving = true;
        return;
    }

    if (!s_nmea_receiving) {
        return;
    }

    if (c == '\r' || c == '\n') {
        if (s_nmea_len > 6) {
            s_nmea_buf[s_nmea_len] = '\0';
            process_sentence(s_nmea_buf, s_nmea_len);
        }
        s_nmea_receiving = false;
        s_nmea_len = 0;
        return;
    }

    if (s_nmea_len < NMEA_MAX_LEN) {
        s_nmea_buf[s_nmea_len++] = c;
    } else {
        // Sentence too long — discard.
        s_nmea_receiving = false;
        s_nmea_len = 0;
    }
}

// ---- UART setup -----------------------------------------------

static void uart_init() {
    Serial2.setRxBufferSize(512);
    Serial2.begin(115200, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
}

// ---- PPS interrupt setup --------------------------------------

static void pps_init() {
    pinMode(PIN_GPS_PPS, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_GPS_PPS), pps_isr, RISING);
}

// ---- FreeRTOS task --------------------------------------------

void gps_task(void* param) {
    (void)param;

    // Register with Task Watchdog.
    esp_task_wdt_add(nullptr);

    while (true) {
        // Read available bytes from UART2.
        int avail = Serial2.available();
        if (avail > 0) {
            for (int i = 0; i < avail; i++) {
                int c = Serial2.read();
                if (c >= 0) {
                    feed_char(static_cast<char>(c));
                }
            }
        } else {
            // No data — yield briefly to avoid busy-spin.
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        // Feed watchdog every iteration.
        esp_task_wdt_reset();
    }
}

// ---- Public init ----------------------------------------------

void gps_init(QueueHandle_t gps_queue) {
    s_gps_queue = gps_queue;

    uart_init();
    pps_init();

    // Reset PPS fix index on init.
    s_fix_idx = 0;

    xTaskCreatePinnedToCore(
        gps_task,       // task function
        "task_gps",     // name
        4096,           // stack size (bytes)
        nullptr,        // parameter
        22,             // priority
        nullptr,        // task handle (not needed)
        0               // Core 0
    );
}
