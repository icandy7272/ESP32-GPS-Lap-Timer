// ============================================================
// GET /files/:name — stream VBO file for download
// Also handles 404 / unknown-path routing.
// ============================================================

#include "wifi_internal.h"

#include <Arduino.h>
#include <WebServer.h>
#include <SD.h>

static void handle_files();
static bool validate_filename(const String& name);
static void stream_file_from_sd(const String& path, const String& filename);

void handle_not_found() {
    String uri = server.uri();

    // Route: /files/filename.vbo
    if (uri.startsWith("/files/")) {
        handle_files();
        return;
    }
    server.send(404, "text/plain", "Not found");
}

static void handle_files() {
    String uri = server.uri();
    String filename = uri.substring(7); // strip "/files/"

    // Validate filename: only alphanumeric, underscore, dot, hyphen
    if (!validate_filename(filename)) {
        server.send(400, "text/plain", "Invalid filename");
        return;
    }

    String path = "/sessions/" + filename;
    stream_file_from_sd(path, filename);
}

static bool validate_filename(const String& name) {
    if (name.length() == 0 || name.length() > 64) {
        return false;
    }
    for (unsigned int i = 0; i < name.length(); i++) {
        char c = name.charAt(i);
        bool ok = (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  c == '_' || c == '.' || c == '-';
        if (!ok) { return false; }
    }
    return true;
}

static void stream_file_from_sd(const String& path,
                                const String& filename) {
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        server.send(503, "text/plain", "SD card busy");
        return;
    }

    File f = SD.open(path, FILE_READ);
    if (!f) {
        xSemaphoreGive(spi_mutex);
        server.send(404, "text/plain", "File not found");
        return;
    }

    size_t file_size = f.size();
    xSemaphoreGive(spi_mutex);

    String disposition = "attachment; filename=" + filename;

    server.sendHeader("Content-Disposition", disposition);
    server.setContentLength(file_size);
    server.send(200, "application/octet-stream", "");

    // Stream in chunks, releasing SPI between reads so storage_task
    // can write VBO data without queue overflows during recording.
    uint8_t buf[FILE_CHUNK_SIZE];
    while (true) {
        xSemaphoreTake(spi_mutex, portMAX_DELAY);
        size_t n = f.available() ? f.read(buf, sizeof(buf)) : 0;
        xSemaphoreGive(spi_mutex);

        if (n == 0) { break; }

        String chunk;
        chunk.concat((const char*)buf, n);
        server.sendContent(chunk);
        vTaskDelay(pdMS_TO_TICKS(CHUNK_DELAY_MS));
    }

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    f.close();
    xSemaphoreGive(spi_mutex);
}
