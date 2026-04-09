// ============================================================
// SD card storage module — ESP32-S3 GPS Lap Timer
// Shared module state and stable entry unit.
// ============================================================

#include "storage.h"
#include "storage/storage_internal.h"

bool storage_recovered = false;

SdFat sd;

namespace storage_internal {

FsFile s_vbo_file;
bool s_session_active = false;
uint32_t s_bytes_written = 0;
uint32_t s_last_fsync_ms = 0;
char s_active_track_name[64] = {0};
TrackDefinition s_session_track = {};
char s_session_start_ts[20] = {0};
int64_t s_session_epoch_us = 0;
double s_session_epoch_tod = 0;

}  // namespace storage_internal

uint32_t storage_get_bytes_written() {
    return storage_internal::s_bytes_written;
}
