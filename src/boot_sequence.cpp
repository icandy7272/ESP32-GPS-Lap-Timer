#include "boot_sequence.h"

#include <stdio.h>

#include "pins.h"

namespace {

constexpr uint32_t POWER_STABLE_DELAY_MS = 120;
constexpr uint32_t TFT_RESET_LOW_MS = 20;
constexpr uint32_t TFT_RESET_HIGH_MS = 120;

}  // namespace

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

uint32_t boot_power_stable_delay_ms() {
    return POWER_STABLE_DELAY_MS;
}

uint32_t boot_tft_reset_low_ms() {
    return TFT_RESET_LOW_MS;
}

uint32_t boot_tft_reset_high_ms() {
    return TFT_RESET_HIGH_MS;
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
