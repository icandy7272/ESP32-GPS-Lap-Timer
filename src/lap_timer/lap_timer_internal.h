#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "../lap_timer.h"

namespace lap_timer_internal {

// === TEMPORARY WALKING-TEST VALUES — revert before shipping ===
// HEADING_WINDOW: 60.0f production (GPS heading is garbage below ~3 km/h,
//                 so 180.0 effectively disables the direction gate for
//                 walking tests).
// ARM_DISTANCE_M: 10.0 prod, 50.0 historical. Lowered to 3.0 so a few
//                 steps of walking can arm the line.
// MIN_CROSSING_SPEED_KMH: rejects stationary GPS drift. Drift speed is
//                 reported as 0 by u-blox RMC, walking is ~3-5 km/h,
//                 so 1.0 km/h is a clean split.
static constexpr float HEADING_WINDOW = 180.0f;  // TEST: was 60.0f
static constexpr double ARM_DISTANCE_M = 3.0;    // TEST: was 10.0 (prod), 50.0 (shipping)
static constexpr float MIN_CROSSING_SPEED_KMH = 1.0f;
static constexpr int DEBOUNCE_SAMPLES = 2;
// MIN_LAP_TIME_MS: prod value is 15000 (rejects spurious short laps).
// TEST: lowered to 5000 so walking-scale crossings produce valid laps
// and a real "best lap" reference gets established.
static constexpr int32_t MIN_LAP_TIME_MS = 5000;  // TEST: was 15000
static constexpr float MAX_LAP_RATIO = 1.5f;
static constexpr int SPLINE_HISTORY = 4;
static constexpr int BINARY_SEARCH_ITS = 12;
static constexpr int MAX_LAP_POINTS = 4096;

extern QueueHandle_t s_gps_queue;
extern QueueHandle_t s_vbo_queue;
extern QueueHandle_t s_lap_event_queue;
extern SemaphoreHandle_t s_session_mutex;
extern const TrackDefinition* s_track;

extern GpsPoint s_history[SPLINE_HISTORY];
extern int s_history_count;

extern double s_arm_distance[MAX_SECTORS];
extern bool s_arm_ready[MAX_SECTORS];

extern int s_debounce_remaining[MAX_SECTORS];
extern bool s_debounce_active[MAX_SECTORS];
extern int64_t s_debounce_crossing_us[MAX_SECTORS];
extern double s_debounce_expected_sign[MAX_SECTORS];

extern int s_current_sector;
extern int64_t s_lap_start_us;
extern int64_t s_sector_start_us;
extern int32_t s_best_lap_time_ms;
extern bool s_first_crossing;

extern GpsPoint* s_lap_points;
extern int s_lap_point_count;

void history_push(const GpsPoint* pt);
const GpsPoint* history_get(int index);

void process_line(int line_idx,
                  const DetectionLine* line,
                  const GpsPoint* prev,
                  const GpsPoint* curr);

void handle_finish_crossing(int64_t crossing_us);
void handle_sector_crossing(int line_idx, int64_t crossing_us);

void emit_lap_event(uint8_t event_type, int sector_index, int64_t crossing_us);
bool is_lap_valid(int32_t lap_time_ms);
void update_session_delta(const GpsPoint* curr);

}  // namespace lap_timer_internal
