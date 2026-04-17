#include "storage_internal.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <string.h>

using namespace storage_internal;

bool storage_start_session(const char* track_name) {
    if (s_session_active) {
        Serial.println("[storage] session already active");
        return false;
    }

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        snprintf(s_session_start_ts, sizeof(s_session_start_ts),
                 "%04d%02d%02d_%02d%02d%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                 timeinfo.tm_mday, timeinfo.tm_hour,
                 timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        snprintf(s_session_start_ts, sizeof(s_session_start_ts),
                 "00000000_000000");
    }

    strncpy(s_active_track_name, track_name, sizeof(s_active_track_name) - 1);
    s_active_track_name[sizeof(s_active_track_name) - 1] = '\0';

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    bool ok = s_vbo_file.open(TMP_FILENAME, O_RDWR | O_CREAT | O_TRUNC);
    xSemaphoreGive(spi_mutex);

    if (!ok) {
        Serial.println("[storage] failed to create .tmp file");
        return false;
    }

    s_session_track = active_track;

    write_vbo_header(track_name);

    s_session_active = true;
    s_bytes_written = 0;
    s_last_fsync_ms = millis();
    set_session_epoch(esp_timer_get_time());

    Serial.printf("[storage] session started: %s\n", track_name);
    return true;
}

void storage_end_session() {
    if (!s_session_active) {
        return;
    }

    // Drain any VBO entries queued before the user pressed stop but not
    // yet consumed by storage_task.  Without this, the subsequent
    // s_session_active=false causes storage_task to `continue` past those
    // entries on its next iteration, silently losing the tail of the
    // session (typically 0–3 fixes buffered during a flush_and_sync spike,
    // larger if SD was slow).  lap_timer_task has already stopped queuing
    // new entries (gated on session_state.is_recording), so this loop sees
    // a strictly shrinking queue.  Give lap_timer a GPS interval first to
    // let any in-flight forward_vbo_entry() call complete.
    vTaskDelay(pdMS_TO_TICKS(50));

    VboEntry pending;
    char drain_line_buf[VBO_LINE_BUF_LEN];
    int drained = 0;
    while (xQueueReceive(vbo_write_queue, &pending, 0) == pdTRUE) {
        format_vbo_line(&pending, drain_line_buf, sizeof(drain_line_buf));
        xSemaphoreTake(spi_mutex, portMAX_DELAY);
        size_t n = s_vbo_file.write(drain_line_buf, strlen(drain_line_buf));
        xSemaphoreGive(spi_mutex);
        if (n > 0) {
            s_bytes_written += n;
        }
        drained++;
    }
    if (drained > 0) {
        Serial.printf("[storage] drained %d pending VBO entries at stop\n", drained);
    }

    s_session_active = false;

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    s_vbo_file.write("\r\n[laptiming]\r\n", 15);

    write_laptiming_lines();

    s_vbo_file.flush();
    s_vbo_file.sync();
    s_vbo_file.close();
    xSemaphoreGive(spi_mutex);

    sync_directory("/");

    char final_path[PATH_BUF_LEN];
    build_final_path(final_path, sizeof(final_path));

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    bool renamed = sd.rename(TMP_FILENAME, final_path);
    xSemaphoreGive(spi_mutex);

    if (renamed) {
        sync_directory(SESSIONS_DIR);
        Serial.printf("[storage] session saved: %s (%u bytes)\n",
                      final_path, s_bytes_written);
        write_session_metadata_json(final_path);
    } else {
        Serial.printf("[storage] WARN: rename failed, data in %s\n", TMP_FILENAME);
    }
}

void storage_write_lap_timing(const LapRecord* lap) {
    (void)lap;
}
