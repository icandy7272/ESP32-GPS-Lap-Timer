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
#include <esp_rom_sys.h>
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
#include "boot_log.h"
#include "serial_console.h"
#include "track_creation_feedback.h"

#include <math.h>

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
RTC_NOINIT_ATTR uint32_t rtc_boot_probe_magic;
RTC_NOINIT_ATTR uint8_t rtc_boot_probe_phase;

// --- Default track (loaded from SD or hard-coded fallback) ---

TrackDefinition active_track = {};

// --- Boot status + early hardware prep -----------------------

static constexpr uint8_t kBootBreadcrumbCleared = 0xFF;
static constexpr uint32_t kBootProbeMagic = 0x42505242;

static BootStatus s_boot_status = boot_status_make();
static bool s_boot_status_active = false;
static uint8_t s_previous_boot_stage = kBootBreadcrumbCleared;
static uint8_t s_previous_boot_state = kBootBreadcrumbCleared;
static uint8_t s_previous_boot_probe_phase = kBootProbeCleared;
static char s_serial_console_line[160] = {0};
static size_t s_serial_console_line_len = 0;
static bool s_serial_console_overflow = false;
static uint32_t s_last_stack_snapshot_ms = 0;

static void clear_boot_breadcrumb() {
    rtc_boot_stage = kBootBreadcrumbCleared;
    rtc_boot_state = kBootBreadcrumbCleared;
}

static uint8_t capture_previous_boot_probe() {
    if (rtc_boot_probe_magic != kBootProbeMagic) {
        return kBootProbeCleared;
    }
    return rtc_boot_probe_phase;
}

static void clear_boot_probe() {
    rtc_boot_probe_magic = kBootProbeMagic;
    rtc_boot_probe_phase = kBootProbeCleared;
}

static void boot_probe_mark(EarlyBootProbe probe) {
    rtc_boot_probe_magic = kBootProbeMagic;
    rtc_boot_probe_phase = static_cast<uint8_t>(probe);
    const char* name = boot_probe_name(probe);
    esp_rom_printf("[BOOT-EARLY] %s\r\n", name);

    char msg[48];
    snprintf(msg, sizeof(msg), "[BOOT-EARLY] %s", name);
    boot_log_append(msg);
}

static void report_previous_boot_probe() {
    if (s_previous_boot_probe_phase == kBootProbeCleared) {
        return;
    }

    char probe[32];
    boot_probe_format(s_previous_boot_probe_phase, probe, sizeof(probe));
    char msg[80];
    snprintf(msg, sizeof(msg), "[BOOT] Previous attempt reached %s before reset", probe);
    Serial.println(msg);
    boot_log_append(msg);
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
    boot_log_append(line);

    if (update_display) {
        display_boot_update(s_boot_status);
    }
}

static uint32_t boot_power_hold_ms(esp_reset_reason_t reset_reason) {
    return reset_reason == ESP_RST_POWERON
        ? boot_cold_power_stable_delay_ms()
        : boot_warm_power_stable_delay_ms();
}

static void prepare_early_boot_hardware(uint32_t settle_delay_ms) {
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
    delay(settle_delay_ms);
}

static void prime_tft_cold_boot_power_path() {
    // Field evidence on the breadboard setup shows the TFT module only becomes
    // reliable on a true cold power-on if BL is driven high early and long
    // enough before the normal reset/init sequence begins. Pure extra delay
    // was not sufficient.
    constexpr uint32_t kPrimeLowMs = 120;
    constexpr uint32_t kPrimeHighMs = 120;

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);

    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, LOW);
    delay(kPrimeLowMs);
    digitalWrite(PIN_TFT_BL, HIGH);
    delay(kPrimeHighMs);
    digitalWrite(PIN_TFT_BL, LOW);

    digitalWrite(PIN_LED, LOW);
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

static void serial_console_print_help() {
    Serial.println("[serial] Commands:");
    Serial.println("  help");
    Serial.println("  ls tracks");
    Serial.println("  ls sessions");
    Serial.println("  cat tracks/<filename>");
    Serial.println("  cat sessions/<filename>");
    Serial.println("  track draft <name>   start a new track draft");
    Serial.println("  mark p1              sample 2s and set P1 at current GPS");
    Serial.println("  mark p2              sample 2s and set P2 (auto heading)");
    Serial.println("  track save           persist draft to SD and activate");
    Serial.println("  track cancel         discard draft");
    Serial.println("  track status         print current draft state");
}

static void serial_console_print_invalid(const SerialConsoleCommand& command) {
    if (command.error[0] != '\0') {
        Serial.printf("[serial] ERR: %s\n", command.error);
    } else {
        Serial.println("[serial] ERR: invalid command");
    }
    Serial.println("[serial] Type 'help' for commands");
}

static void serial_console_list_directory(const char* dir_path) {
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("[serial] ERR: SD busy");
        return;
    }

    FsFile dir;
    if (!dir.open(dir_path, O_RDONLY)) {
        xSemaphoreGive(spi_mutex);
        Serial.printf("[serial] ERR: directory not found: %s\n", dir_path);
        return;
    }

    FsFile entry;
    char name[128];
    int count = 0;
    while (entry.openNext(&dir, O_RDONLY)) {
        if (!entry.isDir()) {
            entry.getName(name, sizeof(name));
            Serial.println(name);
            count++;
        }
        entry.close();
    }
    dir.close();
    xSemaphoreGive(spi_mutex);

    Serial.printf("[serial] count=%d\n", count);
}

static void serial_console_cat_file(const char* path) {
    static constexpr size_t kCatLimitBytes = 4096;
    uint8_t buf[128];
    size_t total = 0;
    bool truncated = false;
    bool wrote_data = false;
    char last_byte = '\n';

    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("[serial] ERR: SD busy");
        return;
    }

    FsFile file;
    if (!file.open(path, O_RDONLY)) {
        xSemaphoreGive(spi_mutex);
        Serial.printf("[serial] ERR: file not found: %s\n", path);
        return;
    }
    xSemaphoreGive(spi_mutex);

    Serial.printf("[serial] --- %s ---\n", path);

    while (total < kCatLimitBytes) {
        if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
            Serial.println("\n[serial] ERR: SD busy");
            break;
        }

        size_t remaining = kCatLimitBytes - total;
        size_t want = remaining < sizeof(buf) ? remaining : sizeof(buf);
        int n = file.read(buf, want);
        bool has_more = file.available() > 0;
        xSemaphoreGive(spi_mutex);

        if (n <= 0) {
            break;
        }

        Serial.write(buf, static_cast<size_t>(n));
        total += static_cast<size_t>(n);
        wrote_data = true;
        last_byte = static_cast<char>(buf[n - 1]);
        if (total >= kCatLimitBytes && has_more) {
            truncated = true;
            break;
        }
    }

    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        file.close();
        xSemaphoreGive(spi_mutex);
    }

    if (wrote_data && last_byte != '\n') {
        Serial.println();
    }
    if (truncated) {
        Serial.println("...[TRUNCATED]");
    }
}

// ============================================================
// Track-draft state for `track draft` / `mark p1|p2` / `track save`
// Used by tools/live_map.py so the walker can mark a new start/finish
// segment over USB while the laptop is on a phone hotspot (rather
// than having to switch the laptop Wi-Fi to the ESP32 AP to reach
// the normal web track creation UI).
//
// Session-lifetime state; cleared on successful save, cancel, or
// draft-restart.  No persistence.
// ============================================================

static bool   s_draft_active   = false;
static bool   s_draft_has_p1   = false;
static bool   s_draft_has_p2   = false;
static char   s_draft_name[64] = {0};
static double s_draft_p1_lat   = 0.0;
static double s_draft_p1_lon   = 0.0;
static double s_draft_p2_lat   = 0.0;
static double s_draft_p2_lon   = 0.0;
static float  s_draft_heading  = 0.0f;

static void draft_clear() {
    s_draft_active = false;
    s_draft_has_p1 = false;
    s_draft_has_p2 = false;
    s_draft_name[0] = '\0';
    // Clear the lap-timer's draft validation line so stale candidates
    // from a previous draft session stop firing.
    (void)lap_timer_set_draft_validation_line(nullptr, DRAFT_OWNER_NONE);
}

// Push the current draft (P1/P2/heading) into the lap_timer as the
// draft validation line.  Safe to call at any point — the lap_timer
// side is idempotent and resets its counters/ring buffer on each
// call.  Only installs the line when both endpoints + heading are
// ready; no-ops otherwise so "mark p1" alone doesn't start firing
// garbage candidates against a half-drawn line.
static void draft_install_validation_line_if_ready() {
    if (!s_draft_active || !s_draft_has_p1 || !s_draft_has_p2) {
        return;
    }
    DetectionLine dl = {};
    dl.lat1_deg = s_draft_p1_lat;
    dl.lon1_deg = s_draft_p1_lon;
    dl.lat2_deg = s_draft_p2_lat;
    dl.lon2_deg = s_draft_p2_lon;
    dl.valid_heading_deg = s_draft_heading;
    // Tag the install with SERIAL ownership so cross-surface takeovers
    // (web UI posting a different line) are logged instead of silently
    // replacing state the serial workflow is validating.
    (void)lap_timer_set_draft_validation_line(&dl, DRAFT_OWNER_SERIAL);
}

static void draft_sort_ascending(double* arr, int n) {
    for (int i = 1; i < n; i++) {
        double key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

// Classify a point-spread into a confidence tier, per the
// 2026-04-18 finish-line debugging roadmap.  Keeps the UI-facing
// strings in one place so the serial `[draft]` log and any future
// screen/dashboard renderers agree.
static const char* draft_confidence_tier(double spread_m) {
    if (spread_m <= 1.0) return "High";
    if (spread_m <= 2.0) return "Medium";
    return "Low";
}

// Sample `session_state.gps_lat_deg` / `.gps_lon_deg` every 100 ms for
// 5.0 s (up to 50 points), reject samples without a 3D fix, and return
// the per-axis median plus a point-spread estimate.
//
// The 5 s window (extended from the original 2 s) comes from the
// roadmap: absolute GPS error accumulates over a longer window, so a
// longer sample lets the median settle closer to the true geodetic
// point and gives us enough samples to compute a meaningful spread.
//
// `out_spread_m` returns the largest haversine distance from any
// accepted sample to the reported median — a conservative worst-case
// measure that maps directly to what the user sees on an error-circle
// overlay in the live map.
static bool draft_sample_gps_median(double* out_lat,
                                    double* out_lon,
                                    double* out_spread_m,
                                    int* out_sample_count) {
    constexpr int kMaxSamples = 50;
    double lats[kMaxSamples];
    double lons[kMaxSamples];
    int count = 0;

    for (int i = 0; i < kMaxSamples; i++) {
        double lat = 0.0;
        double lon = 0.0;
        bool fix_ok = false;
        if (xSemaphoreTake(session_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            lat = session_state.gps_lat_deg;
            lon = session_state.gps_lon_deg;
            fix_ok = session_state.gps_fix_ok;
            xSemaphoreGive(session_mutex);
        }
        if (fix_ok && count < kMaxSamples) {
            lats[count] = lat;
            lons[count] = lon;
            count++;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (count < 3) {
        return false;
    }

    // Per-axis median — robust to the occasional u-blox glitch sample.
    // Note: sorting mutates the arrays, so we snapshot them first if
    // we still need the originals for spread.
    double lats_sorted[kMaxSamples];
    double lons_sorted[kMaxSamples];
    for (int i = 0; i < count; i++) {
        lats_sorted[i] = lats[i];
        lons_sorted[i] = lons[i];
    }
    draft_sort_ascending(lats_sorted, count);
    draft_sort_ascending(lons_sorted, count);
    // True median: for even counts, average the two middle elements.
    // The previous `sorted[count/2]` returned the UPPER middle (e.g.
    // the 26th order statistic for a 50-sample capture), which biases
    // the saved P1/P2 point by roughly half a sample and inflates the
    // spread measured against it.  Codex P2 from 2026-04-19.
    double median_lat;
    double median_lon;
    if (count % 2 == 1) {
        median_lat = lats_sorted[count / 2];
        median_lon = lons_sorted[count / 2];
    } else {
        median_lat = 0.5 * (lats_sorted[count / 2 - 1] + lats_sorted[count / 2]);
        median_lon = 0.5 * (lons_sorted[count / 2 - 1] + lons_sorted[count / 2]);
    }
    *out_lat = median_lat;
    *out_lon = median_lon;

    // Spread = max haversine distance from any sample to the median.
    // A worst-case estimate rather than p95 because the operator
    // cares about the circle that CONTAINS all samples, not a
    // statistical inlier bound.  Uses the same flat-earth projection
    // elsewhere in the draft pipeline — fine at metre scales.
    const double lat_ref_rad = median_lat * M_PI / 180.0;
    const double cos_lat = cos(lat_ref_rad);
    double max_spread_m = 0.0;
    for (int i = 0; i < count; i++) {
        const double dx_m = (lons[i] - median_lon) * 111000.0 * cos_lat;
        const double dy_m = (lats[i] - median_lat) * 111000.0;
        const double d_m  = sqrt(dx_m * dx_m + dy_m * dy_m);
        if (d_m > max_spread_m) max_spread_m = d_m;
    }
    if (out_spread_m)      *out_spread_m      = max_spread_m;
    if (out_sample_count)  *out_sample_count  = count;
    return true;
}

static void draft_compute_heading_locked() {
    if (!s_draft_has_p1 || !s_draft_has_p2) {
        return;
    }
    // Flat-earth projection with the midpoint latitude as the
    // reference — fine at the scale of a detection line (metres).
    double lat_ref = (s_draft_p1_lat + s_draft_p2_lat) * 0.5;
    double cos_lat = cos(lat_ref * M_PI / 180.0);
    double dy_m = (s_draft_p2_lat - s_draft_p1_lat) * 111000.0;
    double dx_m = (s_draft_p2_lon - s_draft_p1_lon) * 111000.0 * cos_lat;
    // Compass heading of the P1 -> P2 direction.
    float line_dir = (float)(atan2(dx_m, dy_m) * 180.0 / M_PI);
    // The valid crossing heading is perpendicular to the line.  This
    // matches the default perpendicular the web UI picks before the
    // operator presses "Flip heading".
    float heading = line_dir - 90.0f;
    while (heading < 0.0f)      heading += 360.0f;
    while (heading >= 360.0f)   heading -= 360.0f;
    s_draft_heading = heading;
}

static void serial_console_handle_track_draft(const char* name) {
    draft_clear();
    s_draft_active = true;
    snprintf(s_draft_name, sizeof(s_draft_name), "%s", name);
    Serial.printf("[draft] started: %s\n", name);
}

static void serial_console_handle_track_mark(int which) {
    if (!s_draft_active) {
        Serial.println("[draft] ERR: no active draft — run 'track draft <name>' first");
        return;
    }
    Serial.printf("[draft] sampling P%d for 5.0 s...\n", which);
    double lat = 0.0, lon = 0.0, spread_m = 0.0;
    int sample_count = 0;
    if (!draft_sample_gps_median(&lat, &lon, &spread_m, &sample_count)) {
        Serial.println("[draft] ERR: not enough 3D GPS fixes during sample window");
        return;
    }
    const char* tier = draft_confidence_tier(spread_m);
    if (which == 1) {
        s_draft_p1_lat = lat;
        s_draft_p1_lon = lon;
        s_draft_has_p1 = true;
        // Recompute heading on BOTH branches — not just which==2.  The
        // live-map-driven workflow makes `mark p2` → `mark p1` → `save`
        // a realistic field sequence.  Without this branch, heading
        // stays at the default 0.0f and the saved track persists a
        // bogus valid_heading.  Codex P2 from 2026-04-19 follow-up.
        draft_compute_heading_locked();
        // If P2 was already set (out-of-order marking), install the
        // validation line now — all three fields (P1/P2/heading) are
        // ready.  Otherwise no-op until `mark p2`.
        draft_install_validation_line_if_ready();
        // Append spread and tier AFTER the `(lat, lon)` block so live_map's
        // existing `_DRAFT_P1_RE` regex still matches the coords, and the
        // new `_DRAFT_SPREAD_RE` regex picks up the confidence metadata.
        Serial.printf("[draft] p1 = (%.7f, %.7f) spread=%.2fm tier=%s samples=%d\n",
                      lat, lon, spread_m, tier, sample_count);
    } else {
        s_draft_p2_lat = lat;
        s_draft_p2_lon = lon;
        s_draft_has_p2 = true;
        draft_compute_heading_locked();
        if (s_draft_has_p1) {
            // Auto-install the draft as the lap_timer's validation
            // line — the operator can now walk across it and see
            // PASS/REJECT candidates in live_map (Phase A) or the
            // phone web UI (Phase B) before committing to save.
            draft_install_validation_line_if_ready();
            Serial.printf(
                "[draft] p2 = (%.7f, %.7f) heading=%.1f spread=%.2fm tier=%s samples=%d\n",
                lat, lon, s_draft_heading, spread_m, tier, sample_count);
        } else {
            Serial.printf(
                "[draft] p2 = (%.7f, %.7f) spread=%.2fm tier=%s samples=%d "
                "— warning: p1 not marked\n",
                lat, lon, spread_m, tier, sample_count);
        }
    }
}

static void serial_console_handle_track_save() {
    if (!s_draft_active) {
        Serial.println("[draft] ERR: no active draft");
        return;
    }
    if (!s_draft_has_p1 || !s_draft_has_p2) {
        Serial.println("[draft] ERR: need both P1 and P2 before save");
        return;
    }
    // Belt-and-braces: recompute the heading right before persistence.
    // The mark handlers both call this now, but a "save" that fires
    // without the heading ever having been computed (e.g. some future
    // code path that populates s_draft_p{1,2} directly) would otherwise
    // persist the default 0.0f.  Safe to re-call — it guards on
    // has_p1 && has_p2 internally.
    draft_compute_heading_locked();
    // Match the web-UI server-side minimum so saved tracks are always
    // usable at runtime.  The exact threshold is compile-time selected
    // (5 m production, 2 m walking-test) — see
    // track_creation_min_save_line_length_m().
    double dy_m = (s_draft_p2_lat - s_draft_p1_lat) * 111000.0;
    double cos_lat = cos(((s_draft_p1_lat + s_draft_p2_lat) * 0.5) * M_PI / 180.0);
    double dx_m = (s_draft_p2_lon - s_draft_p1_lon) * 111000.0 * cos_lat;
    double line_len_m = sqrt(dx_m * dx_m + dy_m * dy_m);
    const double min_len_m = track_creation_min_save_line_length_m();
    if (line_len_m < min_len_m) {
        Serial.printf("[draft] ERR: line too short (%.2f m, need >= %.1f m)\n",
                      line_len_m, min_len_m);
        return;
    }

    // Firmware-side save gate: the draft must have been walked across
    // MIN_ACCEPTED_CROSSINGS times (walking=1, production=2) and the
    // installed validation line must still match what we're about to
    // persist.  Prevents the JS UI bypass and any future client (curl,
    // serial script) from saving an unvalidated line.  Codex P1 from
    // 2026-04-19 follow-up review.
    DetectionLine candidate_line = {};
    candidate_line.lat1_deg          = s_draft_p1_lat;
    candidate_line.lon1_deg          = s_draft_p1_lon;
    candidate_line.lat2_deg          = s_draft_p2_lat;
    candidate_line.lon2_deg          = s_draft_p2_lon;
    candidate_line.valid_heading_deg = s_draft_heading;
    const uint32_t min_acc = lap_timer_draft_validation_min_accepted();
    // Endpoint match tolerance: 2 m (generous — lines drift slightly
    // during the walk test due to GPS noise).  Heading: 10° — a flip
    // (±180°) is also accepted inside lines_match() to support Flip
    // Direction workflows.
    if (!lap_timer_draft_validation_passes_gate(
            &candidate_line, /*tol_m=*/2.0, /*tol_deg=*/10.0, min_acc)) {
        DraftValidationSnapshot snap = {};
        lap_timer_get_draft_validation_snapshot(&snap, nullptr, 0);
        Serial.printf(
            "[draft] ERR: not validated — need %u accepted crossings "
            "(have accepted=%u rejected=%u, session %u)\n",
            (unsigned)min_acc,
            (unsigned)snap.accepted, (unsigned)snap.rejected,
            (unsigned)snap.session_id);
        return;
    }

    TrackDefinition td = {};
    snprintf(td.name, sizeof(td.name), "%s", s_draft_name);
    td.start_finish.lat1_deg = s_draft_p1_lat;
    td.start_finish.lon1_deg = s_draft_p1_lon;
    td.start_finish.lat2_deg = s_draft_p2_lat;
    td.start_finish.lon2_deg = s_draft_p2_lon;
    td.start_finish.valid_heading_deg = s_draft_heading;
    td.center_lat_deg = (s_draft_p1_lat + s_draft_p2_lat) * 0.5;
    td.center_lon_deg = (s_draft_p1_lon + s_draft_p2_lon) * 0.5;
    td.sector_count = 1;  // start/finish only; no sector splits

    TrackSaveResult save_result = track_save_detailed(&td);
    if (!track_creation_save_result_succeeded(save_result)) {
        Serial.printf("[draft] ERR: save failed: %s\n",
                      track_creation_save_result_message(save_result));
        return;
    }

    // track_save_detailed assigns the id and appends to s_tracks.  Pick
    // the last one and make it active via the normal lap_timer entry
    // point so the shadow copy and version counter update correctly.
    const TrackDefinition* saved = track_get(track_count() - 1);
    if (saved) {
        lap_timer_set_track(saved);
        // Mirror everything the /api/tracks/select flow does, so the
        // runtime does not end up in a split "lap_timer sees the new
        // track but session_state / runtime ownership still point at
        // the previous one" state.  Without these, /api/status can
        // keep reporting blocked_no_track and the auto-detect task
        // can stomp the just-created track on its next scan.
        // Codex P1 from 2026-04-19 follow-up review.
        if (xSemaphoreTake(session_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            strlcpy(session_state.track_name, saved->name,
                    sizeof(session_state.track_name));
            xSemaphoreGive(session_mutex);
        }
        track_runtime_note_manual_selection(true /* newly_created */);
        Serial.printf("[draft] saved: %s (%s) length=%.2fm heading=%.1f\n",
                      saved->id, saved->name, line_len_m, s_draft_heading);
    } else {
        Serial.println("[draft] WARN: saved but could not re-read track");
    }

    draft_clear();
}

static void serial_console_handle_track_cancel() {
    if (s_draft_active) {
        Serial.println("[draft] cancelled");
    } else {
        Serial.println("[draft] (no active draft)");
    }
    draft_clear();
}

static void serial_console_handle_track_status() {
    if (!s_draft_active) {
        Serial.println("[draft] inactive");
        return;
    }
    Serial.printf("[draft] active name=%s has_p1=%d has_p2=%d\n",
                  s_draft_name, s_draft_has_p1 ? 1 : 0, s_draft_has_p2 ? 1 : 0);
    if (s_draft_has_p1) {
        Serial.printf("[draft] p1 = (%.7f, %.7f)\n",
                      s_draft_p1_lat, s_draft_p1_lon);
    }
    if (s_draft_has_p2) {
        Serial.printf("[draft] p2 = (%.7f, %.7f) heading=%.1f\n",
                      s_draft_p2_lat, s_draft_p2_lon, s_draft_heading);
    }
}

static void serial_console_handle_line(const char* line) {
    SerialConsoleCommand command = serial_console_parse(line);
    switch (command.type) {
        case SerialConsoleCommandType::Help:
            serial_console_print_help();
            return;
        case SerialConsoleCommandType::ListDirectory:
            serial_console_list_directory(command.arg);
            return;
        case SerialConsoleCommandType::CatFile:
            serial_console_cat_file(command.arg);
            return;
        case SerialConsoleCommandType::TrackDraftStart:
            serial_console_handle_track_draft(command.arg);
            return;
        case SerialConsoleCommandType::TrackMarkP1:
            serial_console_handle_track_mark(1);
            return;
        case SerialConsoleCommandType::TrackMarkP2:
            serial_console_handle_track_mark(2);
            return;
        case SerialConsoleCommandType::TrackSave:
            serial_console_handle_track_save();
            return;
        case SerialConsoleCommandType::TrackCancel:
            serial_console_handle_track_cancel();
            return;
        case SerialConsoleCommandType::TrackStatus:
            serial_console_handle_track_status();
            return;
        case SerialConsoleCommandType::Invalid:
        default:
            serial_console_print_invalid(command);
            return;
    }
}

static void serial_console_finish_line() {
    if (s_serial_console_overflow) {
        Serial.println("[serial] ERR: command too long");
    } else if (s_serial_console_line_len > 0) {
        s_serial_console_line[s_serial_console_line_len] = '\0';
        serial_console_handle_line(s_serial_console_line);
    }

    s_serial_console_line_len = 0;
    s_serial_console_line[0] = '\0';
    s_serial_console_overflow = false;
}

static void serial_console_poll() {
    while (Serial.available() > 0) {
        int raw = Serial.read();
        if (raw < 0) {
            return;
        }

        char c = static_cast<char>(raw);
        if (c == '\r' || c == '\n') {
            if (s_serial_console_line_len > 0 || s_serial_console_overflow) {
                serial_console_finish_line();
            }
            continue;
        }

        if (s_serial_console_overflow) {
            continue;
        }

        if (s_serial_console_line_len + 1 >= sizeof(s_serial_console_line)) {
            s_serial_console_overflow = true;
            continue;
        }

        s_serial_console_line[s_serial_console_line_len++] = c;
    }
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
    const esp_reset_reason_t reset_reason = esp_reset_reason();
    const uint32_t power_hold_ms = boot_power_hold_ms(reset_reason);
    s_previous_boot_probe_phase = capture_previous_boot_probe();
    boot_probe_mark(EarlyBootProbe::SETUP_ENTRY);
    if (reset_reason == ESP_RST_POWERON) {
        prime_tft_cold_boot_power_path();
    }

    Serial.begin(115200);
    delay(200);
    boot_probe_mark(EarlyBootProbe::SERIAL_READY);

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    s_boot_status = boot_status_make();
    s_boot_status_active = false;
    s_boot_fix_valid = false;
    s_previous_boot_stage = rtc_boot_stage;
    s_previous_boot_state = rtc_boot_state;
    report_previous_boot_probe();

    boot_publish(BootStage::POWER,
                 BootState::START,
                 "power staging",
                 reset_reason == ESP_RST_POWERON
                    ? "applying early boot safe state (cold power-on hold)"
                    : "applying early boot safe state",
                 false);
    print_boot_info();
    prepare_early_boot_hardware(power_hold_ms);
    boot_probe_mark(EarlyBootProbe::POWER_STABLE);
    char power_ready_detail[80];
    snprintf(power_ready_detail, sizeof(power_ready_detail),
             "rails stable after %u ms hold",
             static_cast<unsigned>(power_hold_ms));
    boot_publish(BootStage::POWER,
                 BootState::OK,
                 "rails stable",
                 power_ready_detail,
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
    boot_probe_mark(EarlyBootProbe::DISPLAY_START);
    display_boot_init();
    boot_probe_mark(EarlyBootProbe::DISPLAY_READY);
    boot_publish(BootStage::DISPLAY_STAGE,
                 BootState::OK,
                 "splash ready",
                 "boot splash ready");

    // --- Config (reads settings.json from SD) ---
    boot_publish(BootStage::STORAGE,
                 BootState::START,
                 "mounting storage",
                 "initializing storage and config");
    boot_probe_mark(EarlyBootProbe::STORAGE_START);
    const bool storage_ok = storage_init();
    if (!storage_ok) {
        boot_publish(BootStage::STORAGE,
                     BootState::WARN,
                     "storage offline",
                     "sd init failed; continuing without storage");
    } else {
        // SD is up — flush all buffered boot lines to boot_log.txt
        boot_log_flush_to_sd();

        const bool config_loaded = config_load();
        boot_publish(BootStage::STORAGE,
                     BootState::OK,
                     config_loaded ? "config loaded" : "defaults loaded",
                     config_loaded ? "sd mounted, config loaded"
                                   : "sd mounted, defaults in use");

        // --- Crash logger: if previous boot was a crash, log to SD ---
        if (boot_reset_reason_is_crash(static_cast<uint32_t>(reset_reason))) {
            const char* reason_str =
                boot_reset_reason_detail(static_cast<uint32_t>(reset_reason));
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

            // Build crash summary for both crash_log.txt and boot_log.txt
            char crash_buf[384];
            int crash_len = snprintf(crash_buf, sizeof(crash_buf),
                     "reason=%s boot_stage=%s boot_state=%s core0=%d core1=%d "
                     "wm:stor=%u disp=%u sess=%u wifi=%u lapt=%u",
                     reason_str, boot_stage_str, boot_state_str,
                     bc0, bc1, ws, wd, wse, ww, wl);

            // Persist to crash_log.txt
            bool persisted = false;
            xSemaphoreTake(spi_mutex, portMAX_DELAY);
            FsFile log;
            if (log.open("crash_log.txt", O_WRONLY | O_CREAT | O_APPEND)) {
                log.write(reinterpret_cast<const uint8_t*>(crash_buf), crash_len);
                log.write(reinterpret_cast<const uint8_t*>("\n"), 1);
                log.sync();
                log.close();
                persisted = true;
            }
            xSemaphoreGive(spi_mutex);

            // Also log to boot_log.txt via the boot log system
            char crash_msg[420];
            snprintf(crash_msg, sizeof(crash_msg),
                     "[BOOT] Previous crash: %s", crash_buf);
            boot_log_append(crash_msg);

            // Only clear RTC breadcrumbs AFTER successful SD write.
            if (persisted) {
                crash_bc_core0 = 0;
                crash_bc_core1 = 0;
                Serial.println(crash_msg);
            } else {
                Serial.printf("[BOOT] WARN: crash detected (%s) at %s/%s but SD write "
                              "failed — breadcrumbs preserved for next boot\n",
                              reason_str, boot_stage_str, boot_state_str);
            }

            // Reset watermarks to sentinel (0 = "not yet sampled this boot")
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

            // Debug: dump start/finish line geometry + OpenStreetMap URLs
            // so the line position can be visually verified on a real map.
            // Distance between lat1/lon1 and lat2/lon2 is the "width" of the
            // detection line. Walking loops < 2x line width away will
            // double-count crossings.
            double lat1 = active_track.start_finish.lat1_deg;
            double lon1 = active_track.start_finish.lon1_deg;
            double lat2 = active_track.start_finish.lat2_deg;
            double lon2 = active_track.start_finish.lon2_deg;
            // Rough line-length estimate in metres:
            //   1 deg lat ≈ 111320 m;  1 deg lon ≈ 111320 * cos(lat).
            double lat_rad = lat1 * 0.017453292519943;
            double dlat_m = (lat2 - lat1) * 111320.0;
            double dlon_m = (lon2 - lon1) * 111320.0 * cos(lat_rad);
            double line_len_m = sqrt(dlat_m * dlat_m + dlon_m * dlon_m);

            Serial.printf("[track] START/FINISH line:\n");
            Serial.printf("[track]   p1 = (%.7f, %.7f)\n", lat1, lon1);
            Serial.printf("[track]   p2 = (%.7f, %.7f)\n", lat2, lon2);
            Serial.printf("[track]   valid_heading = %.1f deg\n",
                          active_track.start_finish.valid_heading_deg);
            Serial.printf("[track]   line length = %.2f m\n", line_len_m);
            // OpenStreetMap URLs — copy to browser/phone to verify position.
            // The /directions URL draws a walking route between the two
            // endpoints, which visually overlays the detection line.
            Serial.printf("[track]   map p1: https://www.openstreetmap.org/?mlat=%.7f&mlon=%.7f#map=20/%.7f/%.7f\n",
                          lat1, lon1, lat1, lon1);
            Serial.printf("[track]   map p2: https://www.openstreetmap.org/?mlat=%.7f&mlon=%.7f#map=20/%.7f/%.7f\n",
                          lat2, lon2, lat2, lon2);
            Serial.printf("[track]   line:   https://www.openstreetmap.org/directions?engine=fossgis_osrm_foot&route=%.7f,%.7f;%.7f,%.7f\n",
                          lat1, lon1, lat2, lon2);
        } else {
            boot_publish(BootStage::STORAGE,
                         BootState::OK,
                         "storage ready",
                         "no tracks on sd; crossing detection disabled");
        }
    }
    boot_probe_mark(EarlyBootProbe::STORAGE_READY);

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
    boot_probe_mark(EarlyBootProbe::GPS_START);
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
    boot_probe_mark(EarlyBootProbe::READY);
    clear_boot_breadcrumb();
    clear_boot_probe();
}

// Stack watermark snapshot — written every 5s by loop(), read by crash logger.
// Survives warm reset so we can see the lowest watermark before a crash.
RTC_NOINIT_ATTR uint16_t wm_storage;
RTC_NOINIT_ATTR uint16_t wm_display;
RTC_NOINIT_ATTR uint16_t wm_session;
RTC_NOINIT_ATTR uint16_t wm_wifi;
RTC_NOINIT_ATTR uint16_t wm_laptimer;

static void snapshot_stack_watermarks() {
    TaskHandle_t h;
    h = xTaskGetHandle("storage");   if (h) wm_storage  = uxTaskGetStackHighWaterMark(h);
    h = xTaskGetHandle("display");   if (h) wm_display  = uxTaskGetStackHighWaterMark(h);
    h = xTaskGetHandle("session");   if (h) wm_session  = uxTaskGetStackHighWaterMark(h);
    h = xTaskGetHandle("wifi");      if (h) wm_wifi     = uxTaskGetStackHighWaterMark(h);
    h = xTaskGetHandle("lap_timer"); if (h) wm_laptimer = uxTaskGetStackHighWaterMark(h);
}

void loop() {
    serial_console_poll();

    const uint32_t now_ms = millis();
    if ((now_ms - s_last_stack_snapshot_ms) >= 5000) {
        // Snapshot stack high-water marks for crash diagnostics.
        // xTaskGetHandle is safe from the Arduino loop task.
        snapshot_stack_watermarks();
        s_last_stack_snapshot_ms = now_ms;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
}
