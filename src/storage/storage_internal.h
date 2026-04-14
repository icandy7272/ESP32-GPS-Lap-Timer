#pragma once

#include "../storage.h"
#include "../track.h"

#include "../sdfat_global.h"
#include <freertos/queue.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t spi_mutex;
extern QueueHandle_t vbo_write_queue;
extern TrackDefinition active_track;

namespace storage_internal {

// BREADBOARD_OVERRIDE: 4 MHz is safe for breadboard wiring.
// Bump to 10-25 MHz once on a real PCB with short traces and ground plane.
static constexpr uint32_t SD_SPI_MHZ = 4;
static constexpr uint32_t FSYNC_INTERVAL_MS = 30000;
static constexpr int VBO_LINE_BUF_LEN = 128;
static constexpr int PATH_BUF_LEN = 128;

static constexpr const char* SESSIONS_DIR = "sessions";
static constexpr const char* TRACKS_DIR = "tracks";
static constexpr const char* TMP_FILENAME = "_recording.vbo.tmp";

extern FsFile s_vbo_file;
extern bool s_session_active;
extern uint32_t s_bytes_written;
extern uint32_t s_last_fsync_ms;
extern char s_active_track_name[64];
extern TrackDefinition s_session_track;
extern char s_session_start_ts[20];
extern int64_t s_session_epoch_us;
extern double s_session_epoch_tod;

bool ensure_directories();
bool recover_tmp_file();

bool has_vbo_extension(const char* name);
void write_vbo_header(const char* track_name);
void format_vbo_line(const VboEntry* entry, char* buf, int buf_len);
void flush_and_sync();
void sync_directory(const char* dir_path);
void build_final_path(char* path, int path_len);
double timestamp_us_to_secs_since_midnight(int64_t timestamp_us);
void set_session_epoch(int64_t first_timestamp_us);
void write_laptiming_lines();
void write_session_metadata_json(const char* vbo_path);

}  // namespace storage_internal
