// ============================================================
// GET /api/sessions — list VBO files on SD
// Enumerates sessions/ directory directly via SdFat.
// No fixed filename buffer or item count limit.
// ============================================================

#include "wifi_internal.h"
#include "../sdfat_global.h"

#include <Arduino.h>
#include <WebServer.h>
#include <string.h>

static bool has_vbo_extension(const char* name) {
    size_t len = strlen(name);
    if (len < 5) { return false; }
    return (strcasecmp(name + len - 4, ".vbo") == 0);
}

void handle_api_sessions() {
    if (is_throttled()) {
        server.send(503, "application/json", "{\"error\":\"busy\"}");
        return;
    }

    String json = "{\"sessions\":[";
    bool first = true;

    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        FsFile dir;
        if (dir.open("sessions", O_RDONLY)) {
            FsFile entry;
            char fname[256];
            while (entry.openNext(&dir, O_RDONLY)) {
                if (!entry.isDir()) {
                    entry.getName(fname, sizeof(fname));
                    if (has_vbo_extension(fname)) {
                        if (!first) { json += ","; }
                        json += "\"";
                        json += fname;
                        json += "\"";
                        first = false;
                    }
                }
                entry.close();
            }
            dir.close();
        }
        xSemaphoreGive(spi_mutex);
    }

    json += "]}";
    server.send(200, "application/json", json);
}
