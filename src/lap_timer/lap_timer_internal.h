#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "../lap_timer.h"

namespace lap_timer_internal {

static constexpr float HEADING_WINDOW = 60.0f;
static constexpr double ARM_DISTANCE_M = 10.0;  // TODO: restore to 50.0 after testing
static constexpr int DEBOUNCE_SAMPLES = 2;
static constexpr int32_t MIN_LAP_TIME_MS = 15000;
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
