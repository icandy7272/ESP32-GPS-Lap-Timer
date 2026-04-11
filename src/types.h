#pragma once

#include <stdint.h>
#include <stdbool.h>

// ============================================================
// Shared data types — ESP32-S3 GPS Lap Timer
// All modules include this header for common structures.
// See docs/ARCHITECTURE.md §4 for field descriptions.
// ============================================================

// --- GPS Point (produced by task_gps, consumed by task_lap_timer) ---

typedef struct {
    double   lat_deg;         // WGS84 decimal degrees, north positive
    double   lon_deg;         // WGS84 decimal degrees, east positive
    float    speed_kmh;       // km/h
    float    heading_deg;     // 0~360, true north = 0
    float    height_m;        // WGS84 altitude, metres
    int      satellites;      // visible satellites
    int64_t  timestamp_us;    // microsecond timestamp (PPS-corrected esp_timer)
    bool     pps_synced;      // true if this fix used PPS correction
    bool     fix_3d;          // true if 3D fix (satellites >= 6)
} GpsPoint;

// --- Detection Line (start/finish line or sector split) ---

typedef struct {
    double lat1_deg;
    double lon1_deg;
    double lat2_deg;
    double lon2_deg;
    float  valid_heading_deg; // legal crossing direction (normal), +/-60 deg window
} DetectionLine;

// --- Track Definition (loaded from SD: tracks/track_NNN.json) ---

#define MAX_SECTORS 4  // max 4 sectors (3 split lines + 1 start/finish)

typedef struct {
    char          id[32];
    char          name[64];
    DetectionLine start_finish;
    DetectionLine sectors[MAX_SECTORS - 1]; // up to 3 sector split lines
    int           sector_count;             // 1~4 (split lines = 0~3)
    double        center_lat_deg;
    double        center_lon_deg;
    float         approx_length_m;
} TrackDefinition;

// --- Lap Record (produced per completed lap by task_session) ---

typedef enum {
    LAP_STATUS_TIMED  = 0,
    LAP_STATUS_SLOW   = 1,   // lap > best * 150%
    LAP_STATUS_SHORT  = 2,   // lap < 15s
    LAP_STATUS_NO_REF = 3,   // first lap, used as initial reference
    LAP_STATUS_OUT    = 4,   // out lap (before first crossing)
} LapStatus;

typedef struct {
    int      lap_number;
    int32_t  lap_time_ms;
    int32_t  sector_times_ms[MAX_SECTORS]; // -1 = incomplete
    int      sector_count;
    uint8_t  status;          // LapStatus
    int64_t  finish_timestamp_us;
} LapRecord;

// --- Session State (written by task_session, read by display/wifi) ---

#define MAX_LAPS_PER_SESSION 100

typedef struct {
    int      current_lap;
    int      best_lap_number;     // -1 = none yet
    int32_t  best_lap_time_ms;    // -1 = none yet
    int32_t  delta_ms;            // positive = slower, negative = faster
    bool     delta_valid;
    bool     off_track;           // lateral distance > 30m
    bool     is_recording;
    bool     gps_fix_ok;
    int      gps_satellites;
    double   gps_lat_deg;            // WGS-84 decimal degrees (for web UI coordinate readout)
    double   gps_lon_deg;            // WGS-84 decimal degrees
    float    speed_kmh;              // current GPS speed (for screen auto-lock)
    int64_t  current_lap_start_us;   // esp_timer when current lap started (for elapsed display)
    LapRecord laps[MAX_LAPS_PER_SESSION];
    int      lap_count;
    char     track_name[64];
} SessionState;

// --- VBO Write Entry (task_lap_timer -> task_storage queue) ---

typedef struct {
    int      satellites;
    int64_t  timestamp_us;
    double   lat_deg;
    double   lon_deg;
    float    speed_kmh;
    float    heading_deg;
    float    height_m;
} VboEntry;

// --- Lap Event (task_lap_timer -> task_session queue) ---

typedef enum {
    LAP_EVENT_SECTOR = 0,
    LAP_EVENT_FINISH = 1,
} LapEventType;

typedef struct {
    uint8_t  event_type;      // LapEventType
    int      sector_index;    // 0 = start/finish
    int64_t  crossing_us;     // precise crossing time (PPS-corrected, microseconds)
} LapEvent;

// --- Button Event (task_button -> task_session / task_display queues) ---

typedef enum {
    BUTTON_SHORT_PRESS = 0,
    BUTTON_LONG_PRESS  = 1,
} ButtonPressType;

typedef enum {
    BUTTON_RECORD = 0,
    BUTTON_SECTOR = 1,
} ButtonId;

typedef struct {
    uint8_t  event_type;      // ButtonPressType
    uint8_t  button_id;       // ButtonId
} ButtonEvent;
