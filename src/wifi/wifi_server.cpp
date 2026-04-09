// ============================================================
// WiFi AP + HTTP server — ESP32-S3 GPS Lap Timer
// Provides: web dashboard, session/track API, VBO download,
// settings management.  Uses ESP32 WebServer library.
//
// Requires: WiFi.h, WebServer.h (Arduino ESP32 core)
// spi_mutex / session_mutex must be created before wifi_init().
// ============================================================

#include "wifi_server.h"
#include "wifi_internal.h"
#include "config.h"
#include "types.h"
#include "pins.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>

// --- HTTP server on port 80 ---
WebServer server(80);

// --- Recording-mode throttle ---
unsigned long last_request_ms = 0;

// ============================================================
// Initialisation
// ============================================================

void wifi_init() {
    // Mount Arduino SD singleton (storage.cpp uses SdFat separately)
    // Both can coexist since they share SPI bus via spi_mutex
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    SD.begin(PIN_SD_CS);
    xSemaphoreGive(spi_mutex);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(app_config.wifi_ssid, app_config.wifi_pass);

    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[wifi] AP started: SSID=%s  IP=%s\n",
                  app_config.wifi_ssid, ip.toString().c_str());

    server.on("/",               HTTP_GET,  handle_root);
    server.on("/api/status",     HTTP_GET,  handle_api_status);
    server.on("/api/sessions",   HTTP_GET,  handle_api_sessions);
    server.on("/api/tracks",        HTTP_GET,  handle_api_tracks);
    server.on("/api/tracks",        HTTP_POST, handle_api_tracks_post);
    server.on("/api/tracks/select", HTTP_POST, handle_api_tracks_select);
    server.on("/api/tracks/delete", HTTP_POST, handle_api_tracks_delete);
    server.on("/api/recording",     HTTP_POST, handle_api_recording);
    server.on("/api/settings",      HTTP_GET,  handle_api_settings_get);
    server.on("/api/settings",      HTTP_POST, handle_api_settings_post);
    server.onNotFound(handle_not_found);

    server.begin();
    Serial.println("[wifi] HTTP server started on port 80");
}

// ============================================================
// FreeRTOS task
// ============================================================

void wifi_task(void* param) {
    (void)param;
    for (;;) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ============================================================
// Throttle helper — limit requests during recording
// ============================================================

bool is_throttled() {
    SessionState ss = read_session_state();
    if (!ss.is_recording) {
        return false;
    }
    unsigned long now = millis();
    if (now - last_request_ms < RECORDING_THROTTLE_MS) {
        return true;
    }
    last_request_ms = now;
    return false;
}

// ============================================================
// Thread-safe session state snapshot
// ============================================================

SessionState read_session_state() {
    SessionState ss;
    if (xSemaphoreTake(session_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        ss = session_state;
        xSemaphoreGive(session_mutex);
    } else {
        memset(&ss, 0, sizeof(ss));
    }
    return ss;
}
