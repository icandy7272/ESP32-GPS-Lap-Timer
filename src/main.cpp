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
#include <esp_system.h>

#include "sdfat_global.h"
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
#include "track_runtime.h"

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

// Stored boot GPS fix for track auto-detect after GPS acquisition
static GpsPoint s_boot_fix;
static bool     s_boot_fix_valid = false;

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
                s_boot_fix = pt;
                s_boot_fix_valid = true;
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

        // --- Crash logger: if previous boot was a crash, log to SD ---
        esp_reset_reason_t rst = esp_reset_reason();
        if (rst == ESP_RST_PANIC || rst == ESP_RST_INT_WDT ||
            rst == ESP_RST_TASK_WDT || rst == ESP_RST_WDT) {
            const char* reason_str = "unknown";
            switch (rst) {
                case ESP_RST_PANIC:    reason_str = "PANIC (Guru Meditation)"; break;
                case ESP_RST_INT_WDT:  reason_str = "INT_WDT (interrupt watchdog)"; break;
                case ESP_RST_TASK_WDT: reason_str = "TASK_WDT (task watchdog)"; break;
                case ESP_RST_WDT:      reason_str = "WDT (other watchdog)"; break;
                default: break;
            }
            // Read per-core breadcrumbs from RTC memory (survives warm reset)
            extern int crash_bc_core0;
            extern int crash_bc_core1;
            int bc0 = crash_bc_core0;
            int bc1 = crash_bc_core1;

            // Read stack watermarks from RTC (last snapshot before crash)
            extern uint16_t wm_storage, wm_display, wm_session, wm_wifi, wm_laptimer;
            uint16_t ws = wm_storage, wd = wm_display, wse = wm_session,
                     ww = wm_wifi, wl = wm_laptimer;

            // Try to persist crash evidence to SD
            bool persisted = false;
            xSemaphoreTake(spi_mutex, portMAX_DELAY);
            FsFile log;
            if (log.open("crash_log.txt", O_WRONLY | O_CREAT | O_APPEND)) {
                char buf[384];
                int n = snprintf(buf, sizeof(buf),
                         "reason=%s core0=%d core1=%d "
                         "wm:stor=%u disp=%u sess=%u wifi=%u lapt=%u\n",
                         reason_str, bc0, bc1, ws, wd, wse, ww, wl);
                log.write(reinterpret_cast<const uint8_t*>(buf), n);
                log.sync();
                log.close();
                persisted = true;
            }
            xSemaphoreGive(spi_mutex);

            // Only clear RTC breadcrumbs AFTER successful SD write.
            // If SD write failed, keep them for the next boot attempt.
            if (persisted) {
                crash_bc_core0 = 0;
                crash_bc_core1 = 0;
                Serial.printf("[BOOT] Previous crash persisted: %s core0=%d core1=%d "
                              "wm:stor=%u disp=%u sess=%u wifi=%u lapt=%u\n",
                              reason_str, bc0, bc1, ws, wd, wse, ww, wl);
            } else {
                Serial.printf("[BOOT] WARN: crash detected (%s) but SD write failed — "
                              "breadcrumbs preserved for next boot\n", reason_str);
            }

            // Reset watermarks to sentinel (0 = "not yet sampled this boot")
            // so crash_log never inherits stale values from a previous boot.
            wm_storage = 0; wm_display = 0; wm_session = 0;
            wm_wifi = 0; wm_laptimer = 0;
        }
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

    // --- Lap timer init (task started AFTER boot GPS wait to avoid queue race) ---
    lap_timer_init(gps_queue, vbo_write_queue, lap_event_queue,
                   session_mutex, &active_track);

    // --- Display init (TFT hardware) — must happen before boot screens ---
    display_init(btn_display_queue, spi_mutex, session_mutex);

    // --- Recovery notification (before splash if session was salvaged) ---
    if (storage_recovered) {
        display_show_recovery();
        delay(2000);
    }

    // --- Boot screen 1: Splash ---
    display_show_splash();
    delay(1000);

    // --- GPS (Core 0 task creates gps_task internally) ---
    gps_init(gps_queue);

    // --- Boot screen 2: GPS searching (poll up to 30s) ---
    // lap_timer_task not yet started, so we're the sole queue consumer
    boot_wait_for_gps(30000);
    delay(500);

    // --- Auto-detect track from GPS position ---
    bool boot_track_confirmed = false;
    if (s_boot_fix_valid) {
        const TrackDefinition* detected =
            track_auto_detect(s_boot_fix.lat_deg, s_boot_fix.lon_deg);
        if (detected) {
            char detected_name[sizeof(session_state.track_name)] = {0};
            track_runtime_sync_detected_track(
                &active_track, detected, detected_name, sizeof(detected_name));
            if (xSemaphoreTake(session_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                strlcpy(session_state.track_name, detected_name,
                        sizeof(session_state.track_name));
                xSemaphoreGive(session_mutex);
            }
            boot_track_confirmed = true;
            Serial.printf("[BOOT] Auto-detected: %s\n", active_track.name);
        } else if (active_track.name[0] == '\0') {
            strncpy(active_track.name, "No Track", sizeof(active_track.name) - 1);
            active_track.name[sizeof(active_track.name) - 1] = '\0';
            Serial.println("[BOOT] No track within 5km");
        }
    }

    // --- NOW start lap_timer_task (after boot GPS wait is done) ---
    xTaskCreatePinnedToCore(lap_timer_task, "lap_timer", 8192,
                            nullptr, 20, nullptr, 0);

    // --- Boot screen 3: Track found ---
    if (track_runtime_should_show_track_found(boot_track_confirmed,
                                              &active_track)) {
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
    xTaskCreatePinnedToCore(wifi_task, "wifi", 12288,
                            nullptr, 5, nullptr, 1);

    Serial.println("[BOOT] All subsystems started");
    Serial.printf("[BOOT] Free heap: %u B  Free PSRAM: %u B\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());
}

// Stack watermark snapshot — written every 5s by loop(), read by crash logger.
// Survives warm reset so we can see the lowest watermark before a crash.
RTC_NOINIT_ATTR uint16_t wm_storage;
RTC_NOINIT_ATTR uint16_t wm_display;
RTC_NOINIT_ATTR uint16_t wm_session;
RTC_NOINIT_ATTR uint16_t wm_wifi;
RTC_NOINIT_ATTR uint16_t wm_laptimer;

void loop() {
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Snapshot stack high-water marks for crash diagnostics.
    // xTaskGetHandle is safe from the Arduino loop task.
    TaskHandle_t h;
    h = xTaskGetHandle("storage");  if (h) wm_storage  = uxTaskGetStackHighWaterMark(h);
    h = xTaskGetHandle("display");  if (h) wm_display  = uxTaskGetStackHighWaterMark(h);
    h = xTaskGetHandle("session");  if (h) wm_session  = uxTaskGetStackHighWaterMark(h);
    h = xTaskGetHandle("wifi");     if (h) wm_wifi     = uxTaskGetStackHighWaterMark(h);
    h = xTaskGetHandle("lap_timer");if (h) wm_laptimer = uxTaskGetStackHighWaterMark(h);
}
