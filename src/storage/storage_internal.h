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

// PCB build: 25 MHz on short traces with ground plane.
// Was 4 MHz BREADBOARD_OVERRIDE for breadboard signal integrity; restored
// post-soldering 2026-04-29. Today's USB-only flash test could not validate
// because SD/GPS are on battery rail; verify boot log on next battery boot.
static constexpr uint32_t SD_SPI_MHZ = 25;
static constexpr uint32_t FSYNC_INTERVAL_MS = 30000;
static constexpr int VBO_LINE_BUF_LEN = 128;
static constexpr int PATH_BUF_LEN = 128;

static constexpr const char* SESSIONS_DIR = "sessions";
static constexpr const char* TRACKS_DIR = "tracks";
static constexpr const char* TMP_FILENAME = "_recording.vbo.tmp";

// Sidecar file written incrementally — one JSONL line per completed
// lap.  Opened at session start alongside the VBO .tmp, appended by
// storage_write_lap_timing() on every lap, flushed+synced per line
// so power-loss cannot lose lap times even if storage_end_session()
// never runs.  At session stop it is renamed to match the final VBO
// name (`YYYYMMDD_..._NNN.lap.jsonl`) for offline recovery tooling.
//
// Format per line:
//   {"lap":N,"lap_time_ms":T,"status":"VALID"|"SHORT"|...,
//    "sectors":[s1,s2,...],"finish_ts_us":X}
//
// Codex-approved "Boil the lake" fix for the 2026-04-20 walking-test
// power-loss gap where a mid-session unplug wiped 5 real laps.
static constexpr const char* LAP_SIDECAR_TMP_FILENAME = "_recording.lap.jsonl";

extern FsFile s_vbo_file;
extern FsFile s_lap_sidecar_file;
extern bool s_lap_sidecar_open;
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
