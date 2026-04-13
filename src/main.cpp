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
#include "boot_status.h"
#include "boot_sequence.h"

// --- Shared FreeRTOS primitives (created once here) ----------

SemaphoreHandle_t spi_mutex     = nullptr;
// session_mutex is created inside session_init()

QueueHandle_t gps_queue          = nullptr;
QueueHandle_t vbo_write_queue    = nullptr;
QueueHandle_t lap_event_queue    = nullptr;
QueueHandle_t btn_session_queue  = nullptr;
QueueHandle_t btn_display_queue  = nullptr;

RTC_NOINIT_ATTR uint8_t rtc_boot_stage;
RTC_NOINIT_ATTR uint8_t rtc_boot_state;

// --- Default track (loaded from SD or hard-coded fallback) ---

TrackDefinition active_track = {};

// --- Boot status + early hardware prep -----------------------

static constexpr uint8_t kBootBreadcrumbCleared = 0xFF;

static BootStatus s_boot_status = boot_status_make();
static bool s_boot_status_active = false;
static uint8_t s_previous_boot_stage = kBootBreadcrumbCleared;
static uint8_t s_previous_boot_state = kBootBreadcrumbCleared;

static void clear_boot_breadcrumb() {
    rtc_boot_stage = kBootBreadcrumbCleared;
    rtc_boot_state = kBootBreadcrumbCleared;
}

static void boot_publish(BootStage stage,
                         BootState state,
                         const char* display_detail,
                         const char* serial_detail,
                         bool update_display = true) {
    const uint32_t now_ms = millis();
    if (!s_boot_status_active || s_boot_status.stage != stage) {
        boot_status_begin(&s_boot_status, stage, now_ms);
        s_boot_status_active = true;
    }

    boot_status_update(&s_boot_status, state, display_detail, serial_detail);
    rtc_boot_stage = static_cast<uint8_t>(s_boot_status.stage);
    rtc_boot_state = static_cast<uint8_t>(s_boot_status.state);

    char line[128];
    boot_status_format_line(s_boot_status, now_ms, line, sizeof(line));
    Serial.println(line);

    if (update_display) {
        display_boot_update(s_boot_status);
    }
}

static void prepare_early_boot_hardware() {
    EarlyBootPinState boot_pins[EARLY_BOOT_PIN_STATE_COUNT];
    size_t count = boot_fill_early_pin_states(boot_pins,
                                              EARLY_BOOT_PIN_STATE_COUNT);
    for (size_t i = 0; i < count; ++i) {
        pinMode(boot_pins[i].pin, OUTPUT);
        digitalWrite(boot_pins[i].pin,
                     boot_pins[i].level_high ? HIGH : LOW);
    }

    // Let the board 3V3 rail, SD module and TFT controller settle
    // before the first shared-SPI transaction.
    delay(boot_power_stable_delay_ms());
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
    uint32_t last_publish_ms = 0;
    int best_sats = 0;
    int last_reported_sats = -1;
    GpsPoint pt;

    while ((millis() - start) < timeout_ms) {
        if (xQueueReceive(gps_queue, &pt, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (pt.satellites > best_sats) {
                best_sats = pt.satellites;
            }
            if (pt.fix_3d) {
                s_boot_fix = pt;
                s_boot_fix_valid = true;
                char display_detail[24];
                char serial_detail[80];
                snprintf(display_detail, sizeof(display_detail), "%d sats", pt.satellites);
                snprintf(serial_detail, sizeof(serial_detail),
                         "3D fix acquired (%d sats)", pt.satellites);
                boot_publish(BootStage::GPS,
                             BootState::OK,
                             display_detail,
                             serial_detail);
                return;
            }
        }

        const uint32_t now_ms = millis();
        if (best_sats != last_reported_sats || (now_ms - last_publish_ms) >= 1000) {
            char display_detail[24];
            char serial_detail[80];
            if (best_sats > 0) {
                snprintf(display_detail, sizeof(display_detail), "%d sats", best_sats);
                snprintf(serial_detail, sizeof(serial_detail),
                         "waiting for fix (%d sats)", best_sats);
            } else {
                snprintf(display_detail, sizeof(display_detail), "waiting for fix");
                snprintf(serial_detail, sizeof(serial_detail), "waiting for fix");
            }
            boot_publish(BootStage::GPS,
                         BootState::START,
                         display_detail,
                         serial_detail);
            last_reported_sats = best_sats;
            last_publish_ms = now_ms;
        }
    }

    char serial_detail[80];
    if (best_sats > 0) {
        snprintf(serial_detail, sizeof(serial_detail),
                 "gps timeout after %u ms (best %d sats)",
                 static_cast<unsigned>(timeout_ms), best_sats);
    } else {
        snprintf(serial_detail, sizeof(serial_detail),
                 "gps timeout after %u ms",
                 static_cast<unsigned>(timeout_ms));
    }
    boot_publish(BootStage::GPS,
                 BootState::WARN,
                 "fix timeout",
                 serial_detail);
}

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    s_boot_status = boot_status_make();
    s_boot_status_active = false;
    s_boot_fix_valid = false;
    s_previous_boot_stage = rtc_boot_stage;
    s_previous_boot_state = rtc_boot_state;

    boot_publish(BootStage::POWER,
                 BootState::START,
                 "power staging",
                 "applying early boot safe state",
                 false);
    print_boot_info();
    prepare_early_boot_hardware();
    boot_publish(BootStage::POWER,
                 BootState::OK,
                 "rails stable",
                 "rails stable",
                 false);

    // --- Create shared primitives ---
    spi_mutex        = xSemaphoreCreateMutex();
    gps_queue        = xQueueCreate(4,   sizeof(GpsPoint));
    vbo_write_queue  = xQueueCreate(256, sizeof(VboEntry));
    lap_event_queue  = xQueueCreate(16,  sizeof(LapEvent));
    btn_session_queue = xQueueCreate(8,  sizeof(ButtonEvent));
    btn_display_queue = xQueueCreate(8,  sizeof(ButtonEvent));

    // Seed runtime defaults before boot display init so the early
    // backlight handoff uses a sane brightness value.
    config_set_defaults();

    // --- Bring up the TFT boot UI before any SD work ---
    boot_publish(BootStage::DISPLAY_STAGE,
                 BootState::START,
                 "starting display",
                 "resetting TFT",
                 false);
    display_boot_init();
    boot_publish(BootStage::DISPLAY_STAGE,
                 BootState::OK,
                 "splash ready",
                 "boot splash ready");

    // --- Config (reads settings.json from SD) ---
    boot_publish(BootStage::STORAGE,
                 BootState::START,
                 "mounting storage",
                 "initializing storage and config");
    const bool storage_ok = storage_init();
    if (!storage_ok) {
        boot_publish(BootStage::STORAGE,
                     BootState::WARN,
                     "storage offline",
                     "sd init failed; continuing without storage");
    } else {
        const bool config_loaded = config_load();
        boot_publish(BootStage::STORAGE,
                     BootState::OK,
                     config_loaded ? "config loaded" : "defaults loaded",
                     config_loaded ? "sd mounted, config loaded"
                                   : "sd mounted, defaults in use");

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
            const char* boot_stage_str =
                boot_stage_name(static_cast<BootStage>(s_previous_boot_stage));
            const char* boot_state_str =
                boot_state_name(static_cast<BootState>(s_previous_boot_state));

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
                         "reason=%s boot_stage=%s boot_state=%s core0=%d core1=%d "
                         "wm:stor=%u disp=%u sess=%u wifi=%u lapt=%u\n",
                         reason_str, boot_stage_str, boot_state_str,
                         bc0, bc1, ws, wd, wse, ww, wl);
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
                Serial.printf("[BOOT] Previous crash persisted: %s boot_stage=%s boot_state=%s "
                              "core0=%d core1=%d "
                              "wm:stor=%u disp=%u sess=%u wifi=%u lapt=%u\n",
                              reason_str, boot_stage_str, boot_state_str,
                              bc0, bc1, ws, wd, wse, ww, wl);
            } else {
                Serial.printf("[BOOT] WARN: crash detected (%s) at %s/%s but SD write "
                              "failed — breadcrumbs preserved for next boot\n",
                              reason_str, boot_stage_str, boot_state_str);
            }

            // Reset watermarks to sentinel (0 = "not yet sampled this boot")
            // so crash_log never inherits stale values from a previous boot.
            wm_storage = 0; wm_display = 0; wm_session = 0;
            wm_wifi = 0; wm_laptimer = 0;
        }

        // --- Track loading ---
        track_init();
        if (track_count() > 0) {
            track_load_first(&active_track);
            char serial_detail[80];
            snprintf(serial_detail, sizeof(serial_detail), "track loaded: %s", active_track.name);
            boot_publish(BootStage::STORAGE,
                         BootState::OK,
                         "track loaded",
                         serial_detail);
        } else {
            boot_publish(BootStage::STORAGE,
                         BootState::OK,
                         "storage ready",
                         "no tracks on sd; crossing detection disabled");
        }
    }

    // --- Delta engine ---
    delta_init();

    // --- Session state machine ---
    session_init(lap_event_queue, btn_session_queue);

    // --- Lap timer init (task started AFTER boot GPS wait to avoid queue race) ---
    lap_timer_init(gps_queue, vbo_write_queue, lap_event_queue,
                   session_mutex, &active_track);

    // --- Recovery notification while staying on the shared boot splash ---
    if (storage_recovered) {
        boot_publish(BootStage::STORAGE,
                     BootState::WARN,
                     "session recovered",
                     "session recovered");
        delay(2000);
    }

    // --- GPS (Core 0 task creates gps_task internally) ---
    boot_publish(BootStage::GPS,
                 BootState::START,
                 "starting gps",
                 "starting UART and GPS task");
    gps_init(gps_queue);

    // --- GPS fix wait (poll up to 30s) ---
    // lap_timer_task not yet started, so we're the sole queue consumer
    boot_wait_for_gps(30000);
    if (!s_boot_fix_valid) {
        delay(500);
    }

    // --- Auto-detect track from GPS position ---
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
            char serial_detail[80];
            snprintf(serial_detail, sizeof(serial_detail), "track matched: %s", active_track.name);
            boot_publish(BootStage::GPS,
                         BootState::OK,
                         "track matched",
                         serial_detail);
            delay(1000);
        } else if (active_track.name[0] == '\0') {
            strncpy(active_track.name, "No Track", sizeof(active_track.name) - 1);
            active_track.name[sizeof(active_track.name) - 1] = '\0';
            track_runtime_note_track_cleared();
            boot_publish(BootStage::GPS,
                         BootState::OK,
                         "track not found",
                         "track autodetect missed");
            delay(500);
        } else {
            char serial_detail[80];
            snprintf(serial_detail, sizeof(serial_detail),
                     "track autodetect missed; keeping %s",
                     active_track.name);
            boot_publish(BootStage::GPS,
                         BootState::OK,
                         "track not found",
                         serial_detail);
            delay(500);
        }
    }

    // --- READY before runtime task startup / display handoff ---
    boot_publish(BootStage::READY,
                 BootState::OK,
                 "entering runtime",
                 "entering runtime");
    delay(500);

    // --- Runtime display handoff ---
    display_init(btn_display_queue, spi_mutex, session_mutex);

    // --- NOW start lap_timer_task (after boot GPS wait is done) ---
    xTaskCreatePinnedToCore(lap_timer_task, "lap_timer", 8192,
                            nullptr, 20, nullptr, 0);

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

    char serial_detail[80];
    snprintf(serial_detail, sizeof(serial_detail),
             "runtime tasks started, heap=%u, psram=%u",
             ESP.getFreeHeap(), ESP.getFreePsram());
    boot_publish(BootStage::READY,
                 BootState::OK,
                 "runtime active",
                 serial_detail,
                 false);
    clear_boot_breadcrumb();
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
