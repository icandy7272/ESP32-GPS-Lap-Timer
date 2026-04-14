#include "boot_sequence.h"

#include <stdio.h>

#ifdef ARDUINO
#include <esp_system.h>
#endif

#include "pins.h"

namespace {

// Warm reset already benefits from rails and external modules that are
// partially or fully up, so keep the delay modest.
constexpr uint32_t WARM_POWER_STABLE_DELAY_MS = 500;
// BREADBOARD_OVERRIDE: on true POWERON cold boots, the SD module may
// share SPI while being powered from a slower external rail. Wait
// substantially longer before the first shared-SPI transaction so the
// TFT init doesn't race an unpowered SD card on the bus.
constexpr uint32_t COLD_POWER_STABLE_DELAY_MS = 2000;
constexpr uint32_t TFT_RESET_LOW_MS = 20;
// ILI9341 datasheet: 120 ms minimum after hardware reset.
// Use 150 ms for breadboard margin.
constexpr uint32_t TFT_RESET_HIGH_MS = 150;

}  // namespace

#ifdef ARDUINO
static_assert(static_cast<uint32_t>(ESP_RST_UNKNOWN) ==
                  static_cast<uint32_t>(BootResetReason::UNKNOWN),
              "BootResetReason must match esp_reset_reason_t");
static_assert(static_cast<uint32_t>(ESP_RST_POWERON) ==
                  static_cast<uint32_t>(BootResetReason::POWERON),
              "BootResetReason must match esp_reset_reason_t");
static_assert(static_cast<uint32_t>(ESP_RST_EXT) ==
                  static_cast<uint32_t>(BootResetReason::EXT),
              "BootResetReason must match esp_reset_reason_t");
static_assert(static_cast<uint32_t>(ESP_RST_SW) ==
                  static_cast<uint32_t>(BootResetReason::SW),
              "BootResetReason must match esp_reset_reason_t");
static_assert(static_cast<uint32_t>(ESP_RST_PANIC) ==
                  static_cast<uint32_t>(BootResetReason::PANIC),
              "BootResetReason must match esp_reset_reason_t");
static_assert(static_cast<uint32_t>(ESP_RST_INT_WDT) ==
                  static_cast<uint32_t>(BootResetReason::INT_WDT),
              "BootResetReason must match esp_reset_reason_t");
static_assert(static_cast<uint32_t>(ESP_RST_TASK_WDT) ==
                  static_cast<uint32_t>(BootResetReason::TASK_WDT),
              "BootResetReason must match esp_reset_reason_t");
static_assert(static_cast<uint32_t>(ESP_RST_WDT) ==
                  static_cast<uint32_t>(BootResetReason::WDT),
              "BootResetReason must match esp_reset_reason_t");
static_assert(static_cast<uint32_t>(ESP_RST_DEEPSLEEP) ==
                  static_cast<uint32_t>(BootResetReason::DEEPSLEEP),
              "BootResetReason must match esp_reset_reason_t");
static_assert(static_cast<uint32_t>(ESP_RST_BROWNOUT) ==
                  static_cast<uint32_t>(BootResetReason::BROWNOUT),
              "BootResetReason must match esp_reset_reason_t");
static_assert(static_cast<uint32_t>(ESP_RST_SDIO) ==
                  static_cast<uint32_t>(BootResetReason::SDIO),
              "BootResetReason must match esp_reset_reason_t");
#endif

size_t boot_fill_early_pin_states(EarlyBootPinState* out, size_t capacity) {
    if (out == nullptr || capacity < EARLY_BOOT_PIN_STATE_COUNT) {
        return 0;
    }

    // Put the shared SPI bus into a safe idle state before the first
    // SD or TFT transaction on a cold power-on.
    out[0] = {PIN_SD_CS, true};
    out[1] = {PIN_TFT_CS, true};
    out[2] = {PIN_TFT_RST, false};
    out[3] = {PIN_TFT_DC, true};
    out[4] = {PIN_TFT_BL, false};
    return EARLY_BOOT_PIN_STATE_COUNT;
}

uint32_t boot_warm_power_stable_delay_ms() {
    return WARM_POWER_STABLE_DELAY_MS;
}

uint32_t boot_cold_power_stable_delay_ms() {
    return COLD_POWER_STABLE_DELAY_MS;
}

uint32_t boot_tft_reset_low_ms() {
    return TFT_RESET_LOW_MS;
}

uint32_t boot_tft_reset_high_ms() {
    return TFT_RESET_HIGH_MS;
}

BootResetReason boot_reset_reason_from_raw(uint32_t raw_reason) {
    switch (raw_reason) {
        case static_cast<uint32_t>(BootResetReason::UNKNOWN):
            return BootResetReason::UNKNOWN;
        case static_cast<uint32_t>(BootResetReason::POWERON):
            return BootResetReason::POWERON;
        case static_cast<uint32_t>(BootResetReason::EXT):
            return BootResetReason::EXT;
        case static_cast<uint32_t>(BootResetReason::SW):
            return BootResetReason::SW;
        case static_cast<uint32_t>(BootResetReason::PANIC):
            return BootResetReason::PANIC;
        case static_cast<uint32_t>(BootResetReason::INT_WDT):
            return BootResetReason::INT_WDT;
        case static_cast<uint32_t>(BootResetReason::TASK_WDT):
            return BootResetReason::TASK_WDT;
        case static_cast<uint32_t>(BootResetReason::WDT):
            return BootResetReason::WDT;
        case static_cast<uint32_t>(BootResetReason::DEEPSLEEP):
            return BootResetReason::DEEPSLEEP;
        case static_cast<uint32_t>(BootResetReason::BROWNOUT):
            return BootResetReason::BROWNOUT;
        case static_cast<uint32_t>(BootResetReason::SDIO):
            return BootResetReason::SDIO;
        default:
            return BootResetReason::OTHER;
    }
}

const char* boot_reset_reason_short(BootResetReason reason) {
    switch (reason) {
        case BootResetReason::UNKNOWN:
            return "UNKNOWN";
        case BootResetReason::POWERON:
            return "POWERON";
        case BootResetReason::EXT:
            return "EXT";
        case BootResetReason::SW:
            return "SW";
        case BootResetReason::PANIC:
            return "PANIC";
        case BootResetReason::INT_WDT:
            return "INT_WDT";
        case BootResetReason::TASK_WDT:
            return "TASK_WDT";
        case BootResetReason::WDT:
            return "WDT";
        case BootResetReason::DEEPSLEEP:
            return "DEEPSLEEP";
        case BootResetReason::BROWNOUT:
            return "BROWNOUT";
        case BootResetReason::SDIO:
            return "SDIO";
        case BootResetReason::OTHER:
        default:
            return "OTHER";
    }
}

const char* boot_reset_reason_short(uint32_t raw_reason) {
    return boot_reset_reason_short(boot_reset_reason_from_raw(raw_reason));
}

const char* boot_reset_reason_detail(BootResetReason reason) {
    switch (reason) {
        case BootResetReason::UNKNOWN:
            return "UNKNOWN";
        case BootResetReason::POWERON:
            return "POWERON (power-on)";
        case BootResetReason::EXT:
            return "EXT (external pin)";
        case BootResetReason::SW:
            return "SW (esp_restart)";
        case BootResetReason::PANIC:
            return "PANIC (Guru Meditation)";
        case BootResetReason::INT_WDT:
            return "INT_WDT (interrupt watchdog)";
        case BootResetReason::TASK_WDT:
            return "TASK_WDT (task watchdog)";
        case BootResetReason::WDT:
            return "WDT (other watchdog)";
        case BootResetReason::DEEPSLEEP:
            return "DEEPSLEEP (wake from deep sleep)";
        case BootResetReason::BROWNOUT:
            return "BROWNOUT (voltage drop)";
        case BootResetReason::SDIO:
            return "SDIO";
        case BootResetReason::OTHER:
        default:
            return "OTHER";
    }
}

const char* boot_reset_reason_detail(uint32_t raw_reason) {
    return boot_reset_reason_detail(boot_reset_reason_from_raw(raw_reason));
}

bool boot_reset_reason_is_crash(BootResetReason reason) {
    switch (reason) {
        case BootResetReason::PANIC:
        case BootResetReason::INT_WDT:
        case BootResetReason::TASK_WDT:
        case BootResetReason::WDT:
        case BootResetReason::BROWNOUT:
            return true;
        default:
            return false;
    }
}

bool boot_reset_reason_is_crash(uint32_t raw_reason) {
    return boot_reset_reason_is_crash(boot_reset_reason_from_raw(raw_reason));
}

const char* boot_probe_name(EarlyBootProbe probe) {
    switch (probe) {
        case EarlyBootProbe::SETUP_ENTRY:
            return "SETUP_ENTRY";
        case EarlyBootProbe::SERIAL_READY:
            return "SERIAL_READY";
        case EarlyBootProbe::POWER_STABLE:
            return "POWER_STABLE";
        case EarlyBootProbe::DISPLAY_START:
            return "DISPLAY_START";
        case EarlyBootProbe::DISPLAY_READY:
            return "DISPLAY_READY";
        case EarlyBootProbe::STORAGE_START:
            return "STORAGE_START";
        case EarlyBootProbe::STORAGE_READY:
            return "STORAGE_READY";
        case EarlyBootProbe::GPS_START:
            return "GPS_START";
        case EarlyBootProbe::READY:
            return "READY";
        default:
            return "UNKNOWN";
    }
}

const char* boot_probe_name(uint8_t raw_probe) {
    return boot_probe_name(static_cast<EarlyBootProbe>(raw_probe));
}

void boot_probe_format(uint8_t raw_probe, char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) {
        return;
    }

    if (raw_probe == kBootProbeCleared) {
        snprintf(out, out_len, "CLEARED");
        return;
    }

    if (raw_probe <= static_cast<uint8_t>(EarlyBootProbe::READY)) {
        snprintf(out, out_len, "%s", boot_probe_name(raw_probe));
        return;
    }

    snprintf(out, out_len, "UNKNOWN(0x%02X)", raw_probe);
}
