// ============================================================
// GET /api/sessions — list VBO files on SD
// ============================================================

#include "wifi_internal.h"

#include <Arduino.h>
#include <WebServer.h>
#include <SD.h>

void handle_api_sessions() {
    if (is_throttled()) {
        server.send(503, "application/json", "{\"error\":\"busy\"}");
        return;
    }

    String json = "{\"sessions\":[";
    bool first = true;

    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        File dir = SD.open("/sessions");
        if (dir && dir.isDirectory()) {
            File entry = dir.openNextFile();
            while (entry) {
                const char* name = entry.name();
                if (strstr(name, ".vbo") != nullptr) {
                    if (!first) { json += ","; }
                    json += "\"";
                    json += name;
                    json += "\"";
                    first = false;
                }
                entry.close();
                entry = dir.openNextFile();
            }
            dir.close();
        }
        xSemaphoreGive(spi_mutex);
    }

    json += "]}";
    server.send(200, "application/json", json);
}
