// ============================================================
// Boot logger — buffers lines in RAM until SD is ready, then
// writes directly to SD on each append for crash resilience.
// Appends to boot_log.txt so multiple boots accumulate.
// Keeps file under 32 KB by truncating old content on overflow.
// ============================================================

#include "boot_log.h"

#include <Arduino.h>
#include <string.h>
#include <esp_system.h>
#include <freertos/semphr.h>

#include "sdfat_global.h"
#include "pins.h"

extern SemaphoreHandle_t spi_mutex;
extern SdFat sd;

namespace {

constexpr size_t BUF_SIZE = 4096;
constexpr size_t MAX_FILE_BYTES = 32768;

char s_buf[BUF_SIZE];
size_t s_pos = 0;
bool s_sd_ready = false;

const char* reset_reason_str() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  return "POWERON";
        case ESP_RST_SW:       return "SW";
        case ESP_RST_PANIC:    return "PANIC";
        case ESP_RST_INT_WDT:  return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT:      return "WDT";
        case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        default:               return "OTHER";
    }
}

void buf_append(const char* text, size_t len) {
    if (s_pos + len >= BUF_SIZE) {
        return;  // silently drop if buffer full
    }
    memcpy(s_buf + s_pos, text, len);
    s_pos += len;
}

// Write a string directly to boot_log.txt (caller must hold spi_mutex)
void sd_write_line(const char* text, size_t len) {
    FsFile f;
    if (f.open("boot_log.txt", O_WRONLY | O_CREAT | O_APPEND)) {
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

    if (!s_sd_ready) {
        // SD not yet available — buffer in RAM
        buf_append(line, len);
        buf_append("\n", 1);
        return;
    }

    // SD is ready — write directly for crash resilience
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    sd_write_line(line, len);
    xSemaphoreGive(spi_mutex);
}

void boot_log_flush_to_sd() {
    if (s_sd_ready) {
        return;  // already flushed
    }

    xSemaphoreTake(spi_mutex, portMAX_DELAY);

    // Truncate if file is getting too large.
    FsFile check;
    bool truncate = false;
    if (check.open("boot_log.txt", O_RDONLY)) {
        truncate = check.size() > MAX_FILE_BYTES;
        check.close();
    }

    FsFile f;
    const auto flags = truncate
        ? (O_WRONLY | O_CREAT | O_TRUNC)
        : (O_WRONLY | O_CREAT | O_APPEND);
    if (f.open("boot_log.txt", flags)) {
        // Boot header
        char hdr[96];
        int n = snprintf(hdr, sizeof(hdr),
                         "\n=== Boot @ %lu ms  reset=%s  heap=%u  psram=%u ===\n",
                         millis(), reset_reason_str(),
                         ESP.getFreeHeap(), ESP.getFreePsram());
        f.write(reinterpret_cast<const uint8_t*>(hdr), n);

        // Buffered lines from before SD was ready
        if (s_pos > 0) {
            f.write(reinterpret_cast<const uint8_t*>(s_buf), s_pos);
        }
        f.sync();
        f.close();
    }

    xSemaphoreGive(spi_mutex);

    s_pos = 0;
    s_sd_ready = true;
}
