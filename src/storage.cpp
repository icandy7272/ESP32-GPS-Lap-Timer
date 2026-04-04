// ============================================================
// SD card storage module — ESP32-S3 GPS Lap Timer
// VBO file writing, power-loss recovery, session management.
// Uses SdFat library on shared SPI bus protected by spi_mutex.
// See docs/ARCHITECTURE.md sections 5 and 7.
// ============================================================

#include "storage.h"
#include "types.h"
#include "pins.h"
#include "track.h"

#include <Arduino.h>
#include <SdFat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_timer.h>

// ---- External shared resources (created in main.cpp) --------

extern SemaphoreHandle_t spi_mutex;
extern QueueHandle_t     vbo_write_queue;
extern TrackDefinition   active_track;

// ---- Constants ----------------------------------------------

static constexpr uint32_t SD_SPI_MHZ       = 25;
static constexpr uint32_t FSYNC_INTERVAL_MS = 30000;
static constexpr int      VBO_LINE_BUF_LEN  = 128;
static constexpr int      PATH_BUF_LEN      = 128;

static const char* SESSIONS_DIR  = "sessions";
static const char* TRACKS_DIR    = "tracks";
static const char* TMP_FILENAME  = "_recording.vbo.tmp";

// ---- Module state -------------------------------------------

SdFat    sd;  // non-static: shared with track.cpp via extern
static FsFile   vbo_file;
static bool     session_active    = false;
static uint32_t bytes_written     = 0;
static uint32_t last_fsync_ms     = 0;
static char     active_track_name[64]  = {0};
static char     session_start_ts[20] = {0};  // "YYYYMMDD_HHMMSS"

// ---- Forward declarations -----------------------------------

static bool     ensure_directories();
static bool     recover_tmp_file();
static bool     parse_creation_line(const char* line,
                                    int* day, int* mon, int* year,
                                    int* hour, int* min, int* sec);
static bool     has_vbo_extension(const char* name);
static void     write_vbo_header(const char* track_name);
static void     format_vbo_line(const VboEntry* entry, char* buf, int buf_len);
static void     flush_and_sync();
static void     sync_directory(const char* dir_path);
static void     build_final_path(char* path, int path_len);
static double   timestamp_us_to_secs_since_midnight(int64_t timestamp_us);
static double   secs_to_hhmmss(double total_secs);
static void     set_session_epoch(int64_t first_timestamp_us);
static void     write_laptiming_lines();

// Session-start epoch for monotonic VBO timestamps.
static int64_t  session_epoch_us  = 0;
static double   session_epoch_tod = 0;

// ============================================================
// Public API
// ============================================================

bool storage_init() {
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    bool ok = sd.begin(SdSpiConfig(PIN_SD_CS, SHARED_SPI, SD_SCK_MHZ(SD_SPI_MHZ)));
    xSemaphoreGive(spi_mutex);

    if (!ok) {
        Serial.println("[storage] SD card init failed");
        return false;
    }
    Serial.println("[storage] SD card mounted");

    if (!ensure_directories()) {
        return false;
    }

    recover_tmp_file();
    return true;
}

// ------------------------------------------------------------

void storage_task(void* param) {
    (void)param;
    VboEntry entry;
    char line_buf[VBO_LINE_BUF_LEN];

    for (;;) {
        if (xQueueReceive(vbo_write_queue, &entry, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!session_active) {
            continue;
        }

        format_vbo_line(&entry, line_buf, sizeof(line_buf));

        xSemaphoreTake(spi_mutex, portMAX_DELAY);
        size_t n = vbo_file.write(line_buf, strlen(line_buf));
        xSemaphoreGive(spi_mutex);

        if (n > 0) {
            bytes_written += n;
        }

        // Periodic fsync every 30 seconds
        uint32_t now = millis();
        if (now - last_fsync_ms >= FSYNC_INTERVAL_MS) {
            last_fsync_ms = now;
            flush_and_sync();
        }
    }
}

// ------------------------------------------------------------

bool storage_start_session(const char* track_name) {
    if (session_active) {
        Serial.println("[storage] session already active");
        return false;
    }

    // Capture start time for filename
    // Use RTC time if available, otherwise millis-based fallback
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        snprintf(session_start_ts, sizeof(session_start_ts),
                 "%04d%02d%02d_%02d%02d%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                 timeinfo.tm_mday, timeinfo.tm_hour,
                 timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        snprintf(session_start_ts, sizeof(session_start_ts),
                 "00000000_000000");
    }

    strncpy(active_track_name, track_name, sizeof(active_track_name) - 1);
    active_track_name[sizeof(active_track_name) - 1] = '\0';

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    bool ok = vbo_file.open(TMP_FILENAME, O_RDWR | O_CREAT | O_TRUNC);
    xSemaphoreGive(spi_mutex);

    if (!ok) {
        Serial.println("[storage] failed to create .tmp file");
        return false;
    }

    write_vbo_header(track_name);

    session_active = true;
    bytes_written  = 0;
    last_fsync_ms  = millis();
    set_session_epoch(esp_timer_get_time());

    Serial.printf("[storage] session started: %s\n", track_name);
    return true;
}

// ------------------------------------------------------------

void storage_end_session() {
    if (!session_active) {
        return;
    }
    session_active = false;

    // Write [laptiming] section — detection line coordinates in angle-minutes
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    vbo_file.write("\r\n[laptiming]\r\n", 15);

    write_laptiming_lines();

    // Final flush + sync
    vbo_file.flush();
    vbo_file.sync();
    vbo_file.close();
    xSemaphoreGive(spi_mutex);

    sync_directory("/");

    // Build final path and rename
    char final_path[PATH_BUF_LEN];
    build_final_path(final_path, sizeof(final_path));

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    // Check for collision: if final path already exists, append suffix
    if (sd.exists(final_path)) {
        // Rare: same-second session. Add _2 suffix before .vbo
        char* dot = strrchr(final_path, '.');
        if (dot) {
            char tmp[PATH_BUF_LEN];
            *dot = '\0';
            snprintf(tmp, sizeof(tmp), "%s_2.vbo", final_path);
            strncpy(final_path, tmp, PATH_BUF_LEN - 1);
        }
    }
    bool renamed = sd.rename(TMP_FILENAME, final_path);
    xSemaphoreGive(spi_mutex);

    if (renamed) {
        sync_directory(SESSIONS_DIR);
        Serial.printf("[storage] session saved: %s (%u bytes)\n",
                      final_path, bytes_written);
    } else {
        Serial.printf("[storage] WARN: rename failed, data in %s\n", TMP_FILENAME);
    }
}

// ------------------------------------------------------------

void storage_write_lap_timing(const LapRecord* lap) {
    // No-op: VBO [laptiming] section contains detection line coordinates,
    // not lap times. Lap times are computed by analysis software from
    // GPS data and detection lines. Coordinates are written in
    // storage_end_session().
    (void)lap;
}

// ------------------------------------------------------------

int storage_list_sessions(char names[][64], int max) {
    int count = 0;

    xSemaphoreTake(spi_mutex, portMAX_DELAY);

    FsFile dir;
    if (!dir.open(SESSIONS_DIR, O_RDONLY)) {
        xSemaphoreGive(spi_mutex);
        return 0;
    }

    FsFile entry;
    while (count < max && entry.openNext(&dir, O_RDONLY)) {
        if (!entry.isDir()) {
            char fname[64];
            entry.getName(fname, sizeof(fname));
            if (has_vbo_extension(fname)) {
                strncpy(names[count], fname, 63);
                names[count][63] = '\0';
                count++;
            }
        }
        entry.close();
    }
    dir.close();

    xSemaphoreGive(spi_mutex);
    return count;
}

// ------------------------------------------------------------

bool storage_get_session_path(const char* name, char* path, int path_len) {
    snprintf(path, path_len, "%s/%s", SESSIONS_DIR, name);

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    bool exists = sd.exists(path);
    xSemaphoreGive(spi_mutex);

    return exists;
}

// ------------------------------------------------------------

uint32_t storage_get_bytes_written() {
    return bytes_written;
}

// ============================================================
// Internal helpers
// ============================================================

static bool has_vbo_extension(const char* name) {
    size_t len = strlen(name);
    if (len < 5) {
        return false;
    }
    return (strcasecmp(name + len - 4, ".vbo") == 0);
}

// ------------------------------------------------------------

static bool ensure_directories() {
    xSemaphoreTake(spi_mutex, portMAX_DELAY);

    if (!sd.exists(SESSIONS_DIR)) {
        if (!sd.mkdir(SESSIONS_DIR)) {
            xSemaphoreGive(spi_mutex);
            Serial.println("[storage] failed to create sessions/");
            return false;
        }
    }
    if (!sd.exists(TRACKS_DIR)) {
        if (!sd.mkdir(TRACKS_DIR)) {
            xSemaphoreGive(spi_mutex);
            Serial.println("[storage] failed to create tracks/");
            return false;
        }
    }

    xSemaphoreGive(spi_mutex);
    return true;
}

// ------------------------------------------------------------

static bool recover_tmp_file() {
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    bool tmp_exists = sd.exists(TMP_FILENAME);
    xSemaphoreGive(spi_mutex);

    if (!tmp_exists) {
        return false;
    }

    Serial.println("[storage] found .tmp file — running recovery");

    // Try to extract creation timestamp from first line of file
    char first_line[80] = {0};
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    FsFile tmp;
    if (tmp.open(TMP_FILENAME, O_RDONLY)) {
        tmp.fgets(first_line, sizeof(first_line));
        tmp.close();
    }
    xSemaphoreGive(spi_mutex);

    // Parse "File created on DD/MM/YYYY at HH:MM:SS"
    char recovery_name[PATH_BUF_LEN];
    int day, mon, year, hour, min, sec;
    bool parsed = parse_creation_line(first_line,
                                      &day, &mon, &year,
                                      &hour, &min, &sec);
    if (parsed) {
        snprintf(recovery_name, sizeof(recovery_name),
                 "%s/%04d%02d%02d_UNKNOWN_%02d%02d%02d_recovered.vbo",
                 SESSIONS_DIR, year, mon, day, hour, min, sec);
    } else {
        snprintf(recovery_name, sizeof(recovery_name),
                 "%s/00000000_UNKNOWN_000000_recovered.vbo",
                 SESSIONS_DIR);
    }

    // Check if final file already exists (rename succeeded before,
    // but .tmp deletion failed)
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    bool final_exists = sd.exists(recovery_name);
    xSemaphoreGive(spi_mutex);

    if (final_exists) {
        // Just delete the orphan .tmp
        xSemaphoreTake(spi_mutex, portMAX_DELAY);
        sd.remove(TMP_FILENAME);
        xSemaphoreGive(spi_mutex);
        Serial.println("[storage] deleted orphan .tmp (final file exists)");
    } else {
        // Rename .tmp to recovered session
        xSemaphoreTake(spi_mutex, portMAX_DELAY);
        sd.rename(TMP_FILENAME, recovery_name);
        xSemaphoreGive(spi_mutex);

        sync_directory(SESSIONS_DIR);
        Serial.printf("[storage] recovered session: %s\n", recovery_name);
    }

    return true;
}

// ------------------------------------------------------------

static bool parse_creation_line(const char* line,
                                int* day, int* mon, int* year,
                                int* hour, int* min, int* sec) {
    // Expected: "File created on DD/MM/YYYY at HH:MM:SS\r\n"
    return (sscanf(line,
                   "File created on %d/%d/%d at %d:%d:%d",
                   day, mon, year, hour, min, sec) == 6);
}

// ------------------------------------------------------------

static void write_vbo_header(const char* track_name) {
    // Get current date/time for file header
    struct tm ti;
    bool have_time = getLocalTime(&ti, 0);

    char header_buf[512];
    int  pos = 0;

    // Creation line
    if (have_time) {
        pos += snprintf(header_buf + pos, sizeof(header_buf) - pos,
                        "File created on %02d/%02d/%04d at %02d:%02d:%02d\r\n",
                        ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900,
                        ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        pos += snprintf(header_buf + pos, sizeof(header_buf) - pos,
                        "File created on 00/00/0000 at 00:00:00\r\n");
    }

    // Blank line
    pos += snprintf(header_buf + pos, sizeof(header_buf) - pos, "\r\n");

    // [header] section
    pos += snprintf(header_buf + pos, sizeof(header_buf) - pos,
                    "[header]\r\n"
                    "satellites\r\n"
                    "time\r\n"
                    "latitude\r\n"
                    "longitude\r\n"
                    "velocity kmh\r\n"
                    "heading\r\n"
                    "height\r\n"
                    "\r\n");

    // [comments] section
    pos += snprintf(header_buf + pos, sizeof(header_buf) - pos,
                    "[comments]\r\n"
                    "Generated by KartGPS v1.0\r\n"
                    "Log Rate (Hz) : 25.00\r\n"
                    "\r\n");

    // [session data] section
    pos += snprintf(header_buf + pos, sizeof(header_buf) - pos,
                    "[session data]\r\n"
                    "name %s\r\n"
                    "\r\n",
                    track_name);

    // [laptiming] placeholder — actual data written on session end
    // (lap times are unknown at session start)

    // [column names] section
    pos += snprintf(header_buf + pos, sizeof(header_buf) - pos,
                    "[column names]\r\n"
                    "sats time lat long velocity heading height\r\n"
                    "\r\n");

    // [data] section marker
    pos += snprintf(header_buf + pos, sizeof(header_buf) - pos,
                    "[data]\r\n");

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    vbo_file.write(header_buf, pos);
    vbo_file.flush();
    vbo_file.sync();
    xSemaphoreGive(spi_mutex);

    bytes_written += pos;
}

// ------------------------------------------------------------

static void format_vbo_line(const VboEntry* entry, char* buf, int buf_len) {
    double secs = timestamp_us_to_secs_since_midnight(entry->timestamp_us);
    double hhmmss = secs_to_hhmmss(secs);

    // VBO coordinate convention:
    //   latitude  = decimal_degrees * 60  (angle-minutes), south negative
    //   longitude = decimal_degrees * -60 (angle-minutes), east NEGATIVE
    double lat_amin = entry->lat_deg * 60.0;
    double lon_amin = entry->lon_deg * -60.0;

    snprintf(buf, buf_len,
             "%03d %09.2f %+012.5f %+012.5f %07.3f %06.2f %+09.2f\r\n",
             entry->satellites,
             hhmmss,
             lat_amin,
             lon_amin,
             (double)entry->speed_kmh,
             (double)entry->heading_deg,
             (double)entry->height_m);
}

// ------------------------------------------------------------

static void set_session_epoch(int64_t first_timestamp_us) {
    session_epoch_us = first_timestamp_us;
    struct tm ti;
    if (getLocalTime(&ti, 0)) {
        session_epoch_tod = ti.tm_hour * 3600.0 + ti.tm_min * 60.0 + ti.tm_sec;
    } else {
        session_epoch_tod = 0.0;
    }
}

static double timestamp_us_to_secs_since_midnight(int64_t timestamp_us) {
    // Derive VBO time purely from the fix's timestamp, not current wall clock.
    // elapsed_us = time since session start (monotonic, immune to queue delay)
    double elapsed_s = (double)(timestamp_us - session_epoch_us) / 1000000.0;
    double tod = session_epoch_tod + elapsed_s;
    // Wrap at midnight (86400s)
    if (tod >= 86400.0) tod -= 86400.0;
    if (tod < 0.0) tod += 86400.0;
    return tod;
}

// Convert seconds-since-midnight to HHMMSS.SS format for VBO.
// Example: 52382.04s -> 143302.04 (14:33:02.04)
static double secs_to_hhmmss(double total_secs) {
    int total_int = (int)total_secs;
    int hh = total_int / 3600;
    int mm = (total_int % 3600) / 60;
    int ss = total_int % 60;
    double frac = total_secs - (double)total_int;
    return (double)(hh * 10000 + mm * 100 + ss) + frac;
}

// Write VBO [laptiming] detection lines in angle-minutes format.
// Called inside spi_mutex with vbo_file open.
static void write_laptiming_lines() {
    char line[128];

    // Start/finish line
    double lat1 = active_track.start_finish.lat1_deg * 60.0;
    double lon1 = active_track.start_finish.lon1_deg * -60.0;
    double lat2 = active_track.start_finish.lat2_deg * 60.0;
    double lon2 = active_track.start_finish.lon2_deg * -60.0;
    snprintf(line, sizeof(line),
             "Start  %+012.5f %+012.5f %+012.5f %+012.5f Start / Finish\r\n",
             lat1, lon1, lat2, lon2);
    vbo_file.write(line, strlen(line));

    // Sector split lines
    for (int i = 0; i < active_track.sector_count - 1; i++) {
        lat1 = active_track.sectors[i].lat1_deg * 60.0;
        lon1 = active_track.sectors[i].lon1_deg * -60.0;
        lat2 = active_track.sectors[i].lat2_deg * 60.0;
        lon2 = active_track.sectors[i].lon2_deg * -60.0;
        snprintf(line, sizeof(line),
                 "Split  %+012.5f %+012.5f %+012.5f %+012.5f Split %d\r\n",
                 lat1, lon1, lat2, lon2, i + 1);
        vbo_file.write(line, strlen(line));
    }

    vbo_file.write("\r\n", 2);
}

// ------------------------------------------------------------

static void flush_and_sync() {
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    vbo_file.flush();
    vbo_file.sync();
    xSemaphoreGive(spi_mutex);
}

// ------------------------------------------------------------

static void sync_directory(const char* dir_path) {
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    FsFile dir;
    if (dir.open(dir_path, O_RDONLY)) {
        dir.sync();
        dir.close();
    }
    xSemaphoreGive(spi_mutex);
}

// ------------------------------------------------------------

static void build_final_path(char* path, int path_len) {
    // Find next sequence number for this track+date combo
    int seq = 1;
    char probe[PATH_BUF_LEN];

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    for (; seq <= 999; seq++) {
        snprintf(probe, sizeof(probe), "%s/%s_%s_%03d.vbo",
                 SESSIONS_DIR, session_start_ts, active_track_name, seq);
        if (!sd.exists(probe)) {
            break;
        }
    }
    xSemaphoreGive(spi_mutex);

    // session_start_ts is "YYYYMMDD_HHMMSS", split into date and time parts
    // Final name: sessions/YYYYMMDD_TrackName_HHMMSS_NNN.vbo
    char date_part[9] = {0};
    char time_part[7] = {0};
    strncpy(date_part, session_start_ts, 8);
    strncpy(time_part, session_start_ts + 9, 6);

    snprintf(path, path_len, "%s/%s_%s_%s_%03d.vbo",
             SESSIONS_DIR, date_part, active_track_name, time_part, seq);
}
