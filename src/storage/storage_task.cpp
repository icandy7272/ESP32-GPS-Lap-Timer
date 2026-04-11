#include "storage_internal.h"

#include <Arduino.h>
#include <string.h>

using namespace storage_internal;

void storage_task(void* param) {
    (void)param;

    VboEntry entry;
    char line_buf[VBO_LINE_BUF_LEN];

    extern int crash_bc_core1;

    for (;;) {
        if (xQueueReceive(vbo_write_queue, &entry, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!s_session_active) {
            continue;
        }

        format_vbo_line(&entry, line_buf, sizeof(line_buf));

        crash_bc_core1 = 70;  // storage: before take spi_mutex
        xSemaphoreTake(spi_mutex, portMAX_DELAY);
        crash_bc_core1 = 71;  // storage: got spi_mutex, before write
        size_t n = s_vbo_file.write(line_buf, strlen(line_buf));
        crash_bc_core1 = 72;  // storage: write done, releasing mutex
        xSemaphoreGive(spi_mutex);

        if (n > 0) {
            s_bytes_written += n;
        }

        uint32_t now = millis();
        if (now - s_last_fsync_ms >= FSYNC_INTERVAL_MS) {
            crash_bc_core1 = 74;  // storage: before flush_and_sync
            s_last_fsync_ms = now;
            flush_and_sync();
            crash_bc_core1 = 75;  // storage: flush_and_sync done
        }

        crash_bc_core1 = 73;  // storage: iteration complete
    }
}
