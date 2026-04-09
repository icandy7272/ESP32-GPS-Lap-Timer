// ============================================================
// GPS Module — ESP32-S3 GPS Lap Timer
// UART2 NMEA reception, PPS hardware interrupt, hand-rolled
// NMEA parser (GGA + RMC), PPS-corrected timestamping.
// ============================================================

#include "gps.h"
#include "config.h"
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
#include <sys/time.h>
#include <time.h>

// ---- Constants ------------------------------------------------

static constexpr int    NMEA_MAX_LEN       = 120;   // max NMEA sentence length
static constexpr int    PPS_STALE_US       = 1100000; // 1100 ms — PPS considered stale
static constexpr int    UART_READ_TIMEOUT  = 10;      // ms, per-character timeout
static constexpr int    WDT_TIMEOUT_SEC    = 5;

static constexpr uint32_t GPS_BAUD_FACTORY = 9600;
static constexpr uint32_t GPS_BAUD_TARGET  = 115200;
static constexpr int      UBX_DETECT_TIMEOUT_MS = 2000;

// Derived at runtime from app_config.gps_rate_hz.
static int     GPS_FIX_RATE_HZ = 25;
static int64_t FIX_INTERVAL_US = 40000;  // 1e6 / GPS_FIX_RATE_HZ

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
static bool     s_rtc_synced   = false;  // true after first GPS->RTC sync
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

/// Sync ESP32 RTC from RMC time (field 0) and date (field 8).
/// Called once per boot on the first valid RMC with a date field.
static void sync_rtc_from_rmc(const char* time_str, const char* date_str) {
    if (s_rtc_synced) {
        return;
    }
    // time_str: "hhmmss.sss", date_str: "ddmmyy"
    if (strlen(time_str) < 6 || strlen(date_str) < 6) {
        return;
    }

    int hour   = (time_str[0] - '0') * 10 + (time_str[1] - '0');
    int minute = (time_str[2] - '0') * 10 + (time_str[3] - '0');
    int second = (time_str[4] - '0') * 10 + (time_str[5] - '0');
    int day    = (date_str[0] - '0') * 10 + (date_str[1] - '0');
    int month  = (date_str[2] - '0') * 10 + (date_str[3] - '0');
    int year   = (date_str[4] - '0') * 10 + (date_str[5] - '0') + 2000;

    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = minute;
    t.tm_sec  = second;

    time_t epoch = mktime(&t);
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    s_rtc_synced = true;
    Serial.println("[gps] RTC synced from GPS");
}

static RmcData parse_rmc(char* body) {
    RmcData result = {};
    char* fields[MAX_FIELDS] = {};
    int count = nmea_split_fields(body, fields, MAX_FIELDS);

    if (count < 9) {
        return result;
    }

    if (fields[1][0] != 'A') {  // 'A' = active, 'V' = void
        return result;
    }

    double speed_knots = atof(fields[6]);
    result.speed_kmh   = static_cast<float>(speed_knots * 1.852);
    result.heading_deg = (fields[7][0] != '\0') ? static_cast<float>(atof(fields[7])) : 0.0f;
    result.valid       = true;

    // Sync ESP32 RTC once from GPS date/time
    sync_rtc_from_rmc(fields[0], fields[8]);

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
    // Use GGA fix_quality as the authoritative fix indicator.
    // Whitelist real navigation fixes only:
    //   1 = GPS, 2 = DGPS, 4 = RTK fixed, 5 = RTK float
    // Reject 6 (estimated/dead-reckoning), 7 (manual), 8 (simulator).
    // Note: field name is "fix_3d" for legacy reasons but actually
    // means "has valid navigation fix" (not strictly 3D vs 2D).
    int fq = gga->fix_quality;
    point.fix_3d       = (fq == 1 || fq == 2 || fq == 4 || fq == 5);
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
    bool queue_was_full = false;
    if (xQueueSend(s_gps_queue, &point, 0) == errQUEUE_FULL) {
        queue_was_full = true;
        GpsPoint discard;
        xQueueReceive(s_gps_queue, &discard, 0);
        xQueueSend(s_gps_queue, &point, 0);
    }

    // Periodic diagnostic — print rate (fixes/sec) and queue health
    static uint32_t last_diag_ms = 0;
    static uint16_t fixes_since_last = 0;
    static uint16_t drops_since_last = 0;
    fixes_since_last++;
    if (queue_was_full) drops_since_last++;
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

// ---- UBX protocol helpers -------------------------------------

// Fletcher-8 checksum over class + id + length + payload bytes.
static void ubx_checksum(const uint8_t* data, int len,
                         uint8_t* ck_a, uint8_t* ck_b) {
    uint8_t a = 0;
    uint8_t b = 0;
    for (int i = 0; i < len; i++) {
        a += data[i];
        b += a;
    }
    *ck_a = a;
    *ck_b = b;
}

// Build and send a complete UBX frame on Serial2.
static void ubx_send(uint8_t cls, uint8_t id,
                     const uint8_t* payload, uint16_t len) {
    uint8_t header[4] = {
        cls, id,
        static_cast<uint8_t>(len & 0xFF),
        static_cast<uint8_t>((len >> 8) & 0xFF)
    };

    uint8_t ck_a = 0;
    uint8_t ck_b = 0;
    ubx_checksum(header, 4, &ck_a, &ck_b);

    for (uint16_t i = 0; i < len; i++) {
        ck_a += payload[i];
        ck_b += ck_a;
    }

    Serial2.write(0xB5);
    Serial2.write(0x62);
    Serial2.write(header, 4);
    if (len > 0 && payload != nullptr) {
        Serial2.write(payload, len);
    }
    Serial2.write(ck_a);
    Serial2.write(ck_b);
    Serial2.flush();
}

// ---- UBX configuration commands -------------------------------

// UBX-CFG-PRT: set UART1 baud rate.
static void ubx_cfg_prt(uint32_t baud) {
    uint8_t payload[20] = {};
    payload[0] = 0x01;  // portID = UART1

    // mode: 8N1 = 0x000008D0
    payload[4] = 0xD0;
    payload[5] = 0x08;

    // baudRate (little-endian)
    payload[8]  = static_cast<uint8_t>(baud);
    payload[9]  = static_cast<uint8_t>(baud >> 8);
    payload[10] = static_cast<uint8_t>(baud >> 16);
    payload[11] = static_cast<uint8_t>(baud >> 24);

    // inProtoMask = 0x0007 (UBX + NMEA + RTCM)
    payload[12] = 0x07;
    payload[13] = 0x00;

    // outProtoMask = 0x0003 (UBX + NMEA)
    payload[14] = 0x03;
    payload[15] = 0x00;

    ubx_send(0x06, 0x00, payload, sizeof(payload));
}

// UBX-CFG-RATE: set measurement rate from Hz.
static void ubx_cfg_rate(uint8_t rate_hz) {
    uint16_t meas_ms = 1000 / rate_hz;
    uint8_t payload[6] = {};

    payload[0] = static_cast<uint8_t>(meas_ms & 0xFF);
    payload[1] = static_cast<uint8_t>((meas_ms >> 8) & 0xFF);
    payload[2] = 0x01;  // navRate = 1
    payload[3] = 0x00;
    payload[4] = 0x01;  // timeRef = GPS time
    payload[5] = 0x00;

    ubx_send(0x06, 0x08, payload, sizeof(payload));
}

// UBX-CFG-MSG: set output rate for one NMEA sentence.
static void ubx_cfg_msg(uint8_t nmea_cls, uint8_t nmea_id,
                        uint8_t rate) {
    uint8_t payload[3] = { nmea_cls, nmea_id, rate };
    ubx_send(0x06, 0x01, payload, sizeof(payload));
}

// Enable GGA + RMC; disable GSV, GSA, GLL, VTG, ZDA.
static void ubx_configure_nmea_sentences() {
    static constexpr uint8_t CLS = 0xF0;

    ubx_cfg_msg(CLS, 0x00, 1);  // GGA on
    ubx_cfg_msg(CLS, 0x04, 1);  // RMC on

    ubx_cfg_msg(CLS, 0x03, 0);  // GSV off
    ubx_cfg_msg(CLS, 0x02, 0);  // GSA off
    ubx_cfg_msg(CLS, 0x01, 0);  // GLL off
    ubx_cfg_msg(CLS, 0x05, 0);  // VTG off
    ubx_cfg_msg(CLS, 0x08, 0);  // ZDA off
}

// UBX-CFG-CFG: save all to BBR + Flash + EEPROM.
static void ubx_cfg_save() {
    uint8_t payload[13] = {};
    // saveMask = 0x0000FFFF
    payload[4] = 0xFF;
    payload[5] = 0xFF;
    // deviceMask = 0x07
    payload[12] = 0x07;

    ubx_send(0x06, 0x09, payload, sizeof(payload));
}

// Return true if NMEA data ('$') arrives within timeout_ms.
static bool uart_detect_nmea(int timeout_ms) {
    unsigned long start = millis();
    while ((millis() - start) < static_cast<unsigned long>(timeout_ms)) {
        if (Serial2.available() > 0) {
            if (Serial2.read() == '$') {
                return true;
            }
        }
        delay(1);
    }
    return false;
}

// ---- UART + UBX configuration ---------------------------------

static void uart_init() {
    Serial2.setRxBufferSize(1024);

    // Read rate from runtime config (single source of truth).
    // Bound to module's supported range (1-25 Hz). Default 25 if invalid.
    int cfg_rate = app_config.gps_rate_hz;
    if (cfg_rate < 1 || cfg_rate > 25) {
        cfg_rate = 25;
    }
    GPS_FIX_RATE_HZ = cfg_rate;
    FIX_INTERVAL_US = 1000000 / cfg_rate;
    Serial.printf("[gps] Target rate from config: %d Hz\n", GPS_FIX_RATE_HZ);

    static constexpr long TARGET_BAUD = 115200;
    static constexpr long KNOWN_BAUDS[] = {115200, 38400, 9600};
    static constexpr int  NUM_BAUDS = 3;

    // Step 1: Detect current module baud rate
    long live_baud = 0;
    for (int i = 0; i < NUM_BAUDS; i++) {
        Serial2.end();
        delay(50);
        Serial2.begin(KNOWN_BAUDS[i], SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
        delay(200);
        Serial.printf("[gps] Trying %ld baud... ", KNOWN_BAUDS[i]);
        if (uart_detect_nmea(1500)) {
            live_baud = KNOWN_BAUDS[i];
            Serial.println("OK");
            break;
        }
        Serial.println("no data");
    }
    if (live_baud == 0) {
        Serial.println("[gps] ERROR: no NMEA at any baud rate");
        Serial2.begin(TARGET_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
        return;
    }

    bool need_save = false;

    // Step 2: Disable unwanted NMEA sentences FIRST (reduce bandwidth)
    Serial.println("[gps] Configuring NMEA (GGA+RMC only)...");
    ubx_configure_nmea_sentences();
    delay(50);

    // Step 3: Change baud to 115200 if not already there
    if (live_baud != TARGET_BAUD) {
        Serial.printf("[gps] Switching baud %ld -> %ld...\n", live_baud, TARGET_BAUD);
        ubx_cfg_prt(TARGET_BAUD);
        delay(100);
        Serial2.end();
        delay(50);
        Serial2.begin(TARGET_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
        delay(100);

        if (uart_detect_nmea(2000)) {
            Serial.println("[gps] Baud switch verified");
            need_save = true;
        } else {
            Serial.printf("[gps] WARN: baud switch failed, staying at %ld\n", live_baud);
            Serial2.end();
            delay(50);
            Serial2.begin(live_baud, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
            delay(100);
        }
    } else {
        Serial.println("[gps] Already at 115200");
    }

    // Step 4: Set measurement rate. Always send the command (cheap, RAM only)
    // but DO NOT mark need_save unless the baud also changed — because if
    // we're already at TARGET_BAUD, the previous boot already saved this rate
    // to flash, so re-saving wears the flash for no gain.
    Serial.printf("[gps] Setting rate to %d Hz%s\n",
                  GPS_FIX_RATE_HZ,
                  need_save ? " (will persist)" : " (RAM only)");
    ubx_cfg_rate(GPS_FIX_RATE_HZ);
    delay(50);

    // Step 5: Persist to module flash only if baud actually changed.
    // Sentence config and rate are re-sent every boot to RAM, which is fine
    // and avoids unnecessary flash wear on the GNSS module.
    if (need_save) {
        Serial.println("[gps] reconfigured: saving to flash");
        ubx_cfg_save();
        delay(100);
    } else {
        Serial.println("[gps] already configured (no flash save)");
    }

    Serial.println("[gps] Init complete");
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
