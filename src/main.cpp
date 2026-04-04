// ============================================================
// ESP32-S3 GPS Lap Timer — Main Entry Point
// Creates FreeRTOS queues/mutexes, initialises all subsystems,
// and launches tasks on Core 0 (GPS timing) and Core 1 (I/O).
// ============================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_task_wdt.h>

#include "pins.h"
#include "types.h"
#include "gps.h"
#include "lap_timer.h"
#include "delta.h"
#include "storage.h"
#include "session.h"
#include "display.h"
#include "button.h"
#include "wifi_server.h"
#include "config.h"
#include "track.h"

// --- Shared FreeRTOS primitives (created once here) ----------

SemaphoreHandle_t spi_mutex     = nullptr;
// session_mutex is created inside session_init()

QueueHandle_t gps_queue          = nullptr;
QueueHandle_t vbo_write_queue    = nullptr;
QueueHandle_t lap_event_queue    = nullptr;
QueueHandle_t btn_session_queue  = nullptr;
QueueHandle_t btn_display_queue  = nullptr;

// --- Default track (loaded from SD or hard-coded fallback) ---

TrackDefinition active_track = {};

// --- Boot splash (shown while subsystems init) ---------------

static void show_splash() {
    // Deassert SD CS before any SPI activity
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);

    // Backlight on
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);
}

static void print_boot_info() {
    Serial.println("========================================");
    Serial.println("  ESP32-S3 GPS Lap Timer v1.0");
    Serial.println("========================================");
    Serial.printf("  Heap : %u B  PSRAM: %u B\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());
    Serial.printf("  CPU  : %u MHz\n", getCpuFrequencyMhz());
    Serial.println("========================================");
}

// --- Arduino entry points ------------------------------------

// --- GPS boot polling: wait for fix, update screen each second ---

static void boot_wait_for_gps(uint32_t timeout_ms) {
    uint32_t start = millis();
    int best_sats = 0;
    GpsPoint pt;

    while ((millis() - start) < timeout_ms) {
        if (xQueueReceive(gps_queue, &pt, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (pt.satellites > best_sats) {
                best_sats = pt.satellites;
            }
            if (pt.fix_3d) {
                display_show_gps_search(pt.satellites);
                Serial.printf("[BOOT] GPS fix acquired: %d sats\n",
                              pt.satellites);
                return;
            }
        }
        // Update screen roughly once per second (200ms queue poll x5)
        display_show_gps_search(best_sats);
    }

    Serial.println("[BOOT] GPS timeout — proceeding without fix");
    display_show_gps_search(best_sats);
}

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    show_splash();
    print_boot_info();

    // --- Create shared primitives ---
    spi_mutex        = xSemaphoreCreateMutex();
    gps_queue        = xQueueCreate(4,   sizeof(GpsPoint));
    vbo_write_queue  = xQueueCreate(256, sizeof(VboEntry));
    lap_event_queue  = xQueueCreate(16,  sizeof(LapEvent));
    btn_session_queue = xQueueCreate(8,  sizeof(ButtonEvent));
    btn_display_queue = xQueueCreate(8,  sizeof(ButtonEvent));

    // --- Config (reads settings.json from SD) ---
    config_set_defaults();
    if (!storage_init()) {
        Serial.println("[BOOT] SD card init FAILED — running without storage");
    } else {
        config_load();
        Serial.println("[BOOT] SD card OK, config loaded");
    }

    // --- Delta engine ---
    delta_init();

    // --- Session state machine ---
    session_init(lap_event_queue, btn_session_queue);

    // --- Track loading ---
    track_init();
    if (track_count() > 0) {
        track_load_first(&active_track);
        Serial.printf("[BOOT] Track loaded: %s\n", active_track.name);
    } else {
        Serial.println("[BOOT] No tracks on SD — crossing detection disabled");
    }

    // --- Lap timer (Core 0 task created inside) ---
    lap_timer_init(gps_queue, vbo_write_queue, lap_event_queue,
                   session_mutex, &active_track);
    xTaskCreatePinnedToCore(lap_timer_task, "lap_timer", 8192,
                            nullptr, 20, nullptr, 0);

    // --- Display init (TFT hardware) — must happen before boot screens ---
    display_init(btn_display_queue, spi_mutex, session_mutex);

    // --- Boot screen 1: Splash ---
    display_show_splash();
    delay(1000);

    // --- GPS (Core 0 task creates gps_task internally) ---
    gps_init(gps_queue);

    // --- Boot screen 2: GPS searching (poll up to 30s) ---
    boot_wait_for_gps(30000);
    delay(500);

    // --- Boot screen 3: Track found ---
    if (active_track.name[0] != '\0') {
        display_show_track_found(active_track.name);
        delay(1500);
    }

    // --- Boot screen 4: Ready ---
    display_show_ready();
    delay(500);

    // --- Storage task (Core 1) ---
    xTaskCreatePinnedToCore(storage_task, "storage", 6144,
                            nullptr, 18, nullptr, 1);

    // --- Session task (Core 1) ---
    xTaskCreatePinnedToCore(session_task, "session", 4096,
                            nullptr, 16, nullptr, 1);

    // --- Display task (Core 1) — starts rendering loop ---
    xTaskCreatePinnedToCore(display_task, "display", 8192,
                            nullptr, 10, nullptr, 1);

    // --- Buttons (Core 1) ---
    button_init(btn_session_queue, btn_display_queue);
    xTaskCreatePinnedToCore(button_task, "button", 2048,
                            nullptr, 8, nullptr, 1);

    // --- WiFi AP + HTTP server (Core 1) ---
    wifi_init();
    xTaskCreatePinnedToCore(wifi_task, "wifi", 8192,
                            nullptr, 5, nullptr, 1);

    Serial.println("[BOOT] All subsystems started");
    Serial.printf("[BOOT] Free heap: %u B  Free PSRAM: %u B\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());
}

void loop() {
    // All work is in FreeRTOS tasks.
    // Arduino loop just feeds the idle watchdog.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
