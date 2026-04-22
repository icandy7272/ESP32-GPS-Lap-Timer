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

    // Open the lap-timing sidecar alongside the VBO .tmp.  Each lap
    // appends one JSONL line + fsync so power-loss mid-session
    // doesn't lose lap times (the 2026-04-20 walking-test failure
    // mode).  If the sidecar fails to open we still record the VBO
    // — lap times will still be in session_state.laps[] and written
    // to [laptiming] on clean stop.
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    s_lap_sidecar_open = s_lap_sidecar_file.open(
        LAP_SIDECAR_TMP_FILENAME, O_RDWR | O_CREAT | O_TRUNC);
    xSemaphoreGive(spi_mutex);
    if (!s_lap_sidecar_open) {
        Serial.println("[storage] WARN: lap sidecar open failed; proceeding without it");
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

bool storage_end_session() {
    Serial.printf("[stop-trace] end_session: entry active=%d t=%lu\n",
                  s_session_active ? 1 : 0, (unsigned long)millis());
    if (!s_session_active) {
        return false;
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
    uint32_t drain_start = millis();
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
    Serial.printf("[stop-trace] end_session: drain_done drained=%d "
                  "took_ms=%lu t=%lu\n",
                  drained, (unsigned long)(millis() - drain_start),
                  (unsigned long)millis());
    if (drained > 0) {
        Serial.printf("[storage] drained %d pending VBO entries at stop\n", drained);
    }

    s_session_active = false;

    uint32_t t0 = millis();
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    Serial.printf("[stop-trace] end_session: spi_mutex_acquired t=%lu\n",
                  (unsigned long)millis());
    s_vbo_file.write("\r\n[laptiming]\r\n", 15);

    write_laptiming_lines();
    Serial.printf("[stop-trace] end_session: laptiming_written "
                  "took_ms=%lu\n",
                  (unsigned long)(millis() - t0));

    uint32_t t_flush = millis();
    s_vbo_file.flush();
    Serial.printf("[stop-trace] end_session: flushed took_ms=%lu\n",
                  (unsigned long)(millis() - t_flush));
    uint32_t t_sync = millis();
    s_vbo_file.sync();
    Serial.printf("[stop-trace] end_session: synced took_ms=%lu\n",
                  (unsigned long)(millis() - t_sync));
    uint32_t t_close = millis();
    s_vbo_file.close();
    Serial.printf("[stop-trace] end_session: closed took_ms=%lu\n",
                  (unsigned long)(millis() - t_close));
    xSemaphoreGive(spi_mutex);

    uint32_t t_dirsync = millis();
    sync_directory("/");
    Serial.printf("[stop-trace] end_session: dirsync_root took_ms=%lu\n",
                  (unsigned long)(millis() - t_dirsync));

    // Be defensive against a missing sessions/ directory at stop time.
    // storage_init() creates it, but if the FAT metadata was damaged or
    // the directory was removed out-of-band, prefer a best-effort
    // recreate over a guaranteed rename failure.
    if (!ensure_directories()) {
        Serial.println("[storage] WARN: sessions/ unavailable at stop");
    }

    char final_path[PATH_BUF_LEN];
    build_final_path(final_path, sizeof(final_path));

    uint32_t t_rename = millis();
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    bool renamed = sd.rename(TMP_FILENAME, final_path);
    xSemaphoreGive(spi_mutex);
    Serial.printf("[stop-trace] end_session: rename renamed=%d "
                  "took_ms=%lu t=%lu\n",
                  renamed ? 1 : 0,
                  (unsigned long)(millis() - t_rename),
                  (unsigned long)millis());

    // Close the sidecar before renaming.  Any truncation/corruption
    // from a concurrent fs access is avoided by draining writes above
    // and closing explicitly here.
    if (s_lap_sidecar_open) {
        xSemaphoreTake(spi_mutex, portMAX_DELAY);
        s_lap_sidecar_file.flush();
        s_lap_sidecar_file.sync();
        s_lap_sidecar_file.close();
        xSemaphoreGive(spi_mutex);
        s_lap_sidecar_open = false;
    }

    if (renamed) {
        // Rename the sidecar to match: replace trailing ".vbo" with
        // ".lap.jsonl".  On failure we still leave the .tmp sidecar
        // in place; offline tooling can pair by name + mtime.
        char sidecar_final[PATH_BUF_LEN];
        int flen = (int)strlen(final_path);
        if (flen > 4 && strcmp(final_path + flen - 4, ".vbo") == 0
            && flen - 4 + (int)strlen(".lap.jsonl") < (int)sizeof(sidecar_final)) {
            memcpy(sidecar_final, final_path, flen - 4);
            strcpy(sidecar_final + flen - 4, ".lap.jsonl");
            xSemaphoreTake(spi_mutex, portMAX_DELAY);
            bool side_ok = sd.rename(LAP_SIDECAR_TMP_FILENAME, sidecar_final);
            xSemaphoreGive(spi_mutex);
            if (!side_ok) {
                Serial.printf("[storage] WARN: lap sidecar rename failed "
                              "(%s -> %s), tmp preserved\n",
                              LAP_SIDECAR_TMP_FILENAME, sidecar_final);
            }
        }
        uint32_t t_dirsync2 = millis();
        sync_directory(SESSIONS_DIR);
        Serial.printf("[stop-trace] end_session: dirsync_sessions "
                      "took_ms=%lu\n",
                      (unsigned long)(millis() - t_dirsync2));
        Serial.printf("[storage] session saved: %s (%u bytes)\n",
                      final_path, s_bytes_written);
        uint32_t t_meta = millis();
        write_session_metadata_json(final_path);
        Serial.printf("[stop-trace] end_session: metadata_written "
                      "took_ms=%lu\n",
                      (unsigned long)(millis() - t_meta));
    } else {
        Serial.printf("[storage] WARN: rename failed, data in %s\n", TMP_FILENAME);
        // Leave the sidecar at LAP_SIDECAR_TMP_FILENAME too — both
        // tmps pair up for offline recovery.
    }
    return renamed;
}

static const char* lap_status_name(uint8_t status) {
    switch (status) {
        case LAP_STATUS_TIMED:  return "TIMED";
        case LAP_STATUS_SLOW:   return "SLOW";
        case LAP_STATUS_SHORT:  return "SHORT";
        case LAP_STATUS_NO_REF: return "NO_REF";
        case LAP_STATUS_OUT:    return "OUT";
        default:                return "UNKNOWN";
    }
}

void storage_write_lap_timing(const LapRecord* lap) {
    // Append one JSONL line per completed lap to the sidecar.  Synced
    // per line so a power-cut at any point keeps all previously
    // completed laps on disk.  If the sidecar isn't open (fs error at
    // session-start, or session not active yet), silently skip —
    // session_state.laps[] still has the record for the clean-stop
    // path to pick up later.
    if (lap == nullptr) return;
    if (!s_session_active || !s_lap_sidecar_open) return;

    char line[256];
    int pos = snprintf(line, sizeof(line),
                       "{\"lap\":%d,\"lap_time_ms\":%ld,\"status\":\"%s\","
                       "\"finish_ts_us\":%lld,\"sectors\":[",
                       lap->lap_number,
                       (long)lap->lap_time_ms,
                       lap_status_name(lap->status),
                       (long long)lap->finish_timestamp_us);
    int sectors_to_write = lap->sector_count;
    if (sectors_to_write > MAX_SECTORS) sectors_to_write = MAX_SECTORS;
    for (int si = 0; si < sectors_to_write; si++) {
        int n = snprintf(line + pos, sizeof(line) - pos, "%s%ld",
                         (si == 0) ? "" : ",",
                         (long)lap->sector_times_ms[si]);
        if (n <= 0 || n >= (int)(sizeof(line) - pos)) break;
        pos += n;
    }
    // Close JSON + newline.  Reserve 4 bytes: `]}\r\n` + NUL.
    if (pos >= (int)sizeof(line) - 4) {
        pos = (int)sizeof(line) - 4;
    }
    line[pos++] = ']';
    line[pos++] = '}';
    line[pos++] = '\r';
    line[pos++] = '\n';

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    size_t written = s_lap_sidecar_file.write(line, pos);
    // fsync EVERY lap write: durability > throughput.  Lap events
    // happen on the order of minutes; the extra SD latency (~10-50 ms
    // on a decent card) is harmless outside the GPS hot path, and
    // tasks that care about timing are not blocked on this mutex.
    s_lap_sidecar_file.flush();
    s_lap_sidecar_file.sync();
    xSemaphoreGive(spi_mutex);

    if (written != (size_t)pos) {
        Serial.printf("[storage] WARN: lap sidecar short write %zu/%d\n",
                      written, pos);
    }
}
