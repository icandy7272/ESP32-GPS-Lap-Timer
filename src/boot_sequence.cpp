#include "boot_sequence.h"

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
