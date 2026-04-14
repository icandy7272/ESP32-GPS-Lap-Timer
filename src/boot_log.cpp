// ============================================================
// Boot logger — buffers lines in RAM until SD is ready, then
// writes directly to SD on each append for crash resilience.
// Appends to boot_log.txt so multiple boots accumulate.
// Keeps file under 32 KB by truncating old content on overflow.
// ============================================================

#include "boot_log.h"
#include "boot_log_buffer.h"
#include "boot_log_policy.h"
#include "boot_sequence.h"

#include <Arduino.h>
#include <string.h>
#include <esp_system.h>
#include <freertos/semphr.h>

#include "sdfat_global.h"

extern SemaphoreHandle_t spi_mutex;
extern SdFat sd;

namespace {

constexpr size_t MAX_FILE_BYTES = 32768;
constexpr const char* BOOT_LOG_FILENAME = "boot_log.txt";
constexpr const char* BOOT_LOG_ROLLOVER_FILENAME = "boot_log.prev";
constexpr size_t BOOT_LOG_COPY_CHUNK_BYTES = 256;

boot_log_buffer::State s_buffer{};

TickType_t boot_log_spi_timeout_ticks() {
    return pdMS_TO_TICKS(boot_log_policy::spi_mutex_timeout_ms());
}

bool copy_boot_log_tail(FsFile* source, FsFile* dest, size_t tail_bytes_to_keep) {
    if (source == nullptr || dest == nullptr) {
        return false;
    }
    if (tail_bytes_to_keep == 0) {
        return true;
    }

    const size_t source_bytes = source->size();
    const size_t start_offset =
        source_bytes > tail_bytes_to_keep ? source_bytes - tail_bytes_to_keep : 0;
    if (!source->seekSet(start_offset)) {
        return false;
    }

    uint8_t chunk[BOOT_LOG_COPY_CHUNK_BYTES];
    size_t remaining = source_bytes - start_offset;
    while (remaining > 0) {
        const size_t chunk_bytes =
            remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        const int bytes_read = source->read(chunk, chunk_bytes);
        if (bytes_read <= 0) {
            return false;
        }
        if (dest->write(chunk, static_cast<size_t>(bytes_read)) !=
            static_cast<size_t>(bytes_read)) {
            return false;
        }
        remaining -= static_cast<size_t>(bytes_read);
    }

    return true;
}

bool roll_boot_log_history(size_t existing_file_bytes, size_t pending_append_bytes) {
    const size_t tail_bytes_to_keep =
        boot_log_policy::history_tail_bytes_to_keep(MAX_FILE_BYTES,
                                                    pending_append_bytes);

    (void)sd.remove(BOOT_LOG_ROLLOVER_FILENAME);
    if (!sd.rename(BOOT_LOG_FILENAME, BOOT_LOG_ROLLOVER_FILENAME)) {
        return false;
    }

    FsFile source;
    FsFile dest;
    bool ok = false;

    if (source.open(BOOT_LOG_ROLLOVER_FILENAME, O_RDONLY) &&
        dest.open(BOOT_LOG_FILENAME, O_WRONLY | O_CREAT | O_TRUNC)) {
        ok = copy_boot_log_tail(&source,
                                &dest,
                                tail_bytes_to_keep > existing_file_bytes
                                    ? existing_file_bytes
                                    : tail_bytes_to_keep);
        if (ok) {
            ok = dest.sync();
        }
    }

    source.close();
    dest.close();

    if (ok) {
        (void)sd.remove(BOOT_LOG_ROLLOVER_FILENAME);
        return true;
    }

    (void)sd.remove(BOOT_LOG_FILENAME);
    (void)sd.rename(BOOT_LOG_ROLLOVER_FILENAME, BOOT_LOG_FILENAME);
    return false;
}

// Write a string directly to boot_log.txt (caller must hold spi_mutex)
void sd_write_line(const char* text, size_t len) {
    FsFile f;
    if (f.open(BOOT_LOG_FILENAME, O_WRONLY | O_CREAT | O_APPEND)) {
        f.write(reinterpret_cast<const uint8_t*>(text), len);
        f.write(reinterpret_cast<const uint8_t*>("\n"), 1);
        f.sync();
        f.close();
    }
}

}  // namespace

void boot_log_append(const char* line) {
    if (line == nullptr) {
        return;
    }
    const size_t len = strlen(line);

    if (!s_buffer.sd_ready) {
        // SD not yet available — buffer in RAM
        boot_log_buffer::append_line(&s_buffer, line);
        return;
    }

    // SD is ready — write directly for crash resilience
    if (!xSemaphoreTake(spi_mutex, boot_log_spi_timeout_ticks())) {
        return;  // best-effort logging: drop this line rather than block forever
    }
    sd_write_line(line, len);
    xSemaphoreGive(spi_mutex);
}

void boot_log_flush_to_sd() {
    if (s_buffer.sd_ready) {
        return;  // already flushed
    }

    if (!xSemaphoreTake(spi_mutex, boot_log_spi_timeout_ticks())) {
        return;  // preserve buffered lines for a later retry
    }

    // Truncate if file is getting too large.
    FsFile check;
    size_t existing_file_bytes = 0;
    bool roll_history = false;
    if (check.open(BOOT_LOG_FILENAME, O_RDONLY)) {
        existing_file_bytes = check.size();
        roll_history =
            boot_log_policy::should_roll_history(existing_file_bytes, MAX_FILE_BYTES);
        check.close();
    }

    char hdr[96];
    int n = snprintf(hdr, sizeof(hdr),
                     "\n=== Boot @ %lu ms  reset=%s  heap=%u  psram=%u ===\n",
                     millis(),
                     boot_reset_reason_short(
                         static_cast<uint32_t>(esp_reset_reason())),
                     ESP.getFreeHeap(), ESP.getFreePsram());
    const size_t pending_append_bytes =
        static_cast<size_t>(n > 0 ? n : 0) + s_buffer.size;
    if (roll_history) {
        (void)roll_boot_log_history(existing_file_bytes, pending_append_bytes);
    }

    FsFile f;
    bool flush_succeeded = false;
    if (f.open(BOOT_LOG_FILENAME, O_WRONLY | O_CREAT | O_APPEND)) {
        // Boot header
        f.write(reinterpret_cast<const uint8_t*>(hdr), n);

        // Buffered lines from before SD was ready
        if (s_buffer.size > 0) {
            f.write(reinterpret_cast<const uint8_t*>(s_buffer.data), s_buffer.size);
        }
        f.sync();
        f.close();
        flush_succeeded = true;
    }

    xSemaphoreGive(spi_mutex);

    boot_log_buffer::finish_flush(&s_buffer, flush_succeeded);
}
