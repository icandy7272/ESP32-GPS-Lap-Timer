#include "storage_internal.h"

#include <Arduino.h>
#include <string.h>

using namespace storage_internal;

void storage_task(void* param) {
    (void)param;

    VboEntry entry;
    char line_buf[VBO_LINE_BUF_LEN];

    for (;;) {
        if (xQueueReceive(vbo_write_queue, &entry, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!s_session_active) {
            continue;
        }

        format_vbo_line(&entry, line_buf, sizeof(line_buf));

        xSemaphoreTake(spi_mutex, portMAX_DELAY);
        size_t n = s_vbo_file.write(line_buf, strlen(line_buf));
        xSemaphoreGive(spi_mutex);

        if (n > 0) {
            s_bytes_written += n;
        }

        uint32_t now = millis();
        if (now - s_last_fsync_ms >= FSYNC_INTERVAL_MS) {
            s_last_fsync_ms = now;
            flush_and_sync();
        }
    }
}
