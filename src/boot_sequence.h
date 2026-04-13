#pragma once

#include <stddef.h>
#include <stdint.h>

struct EarlyBootPinState {
    int  pin;
    bool level_high;
};

constexpr size_t EARLY_BOOT_PIN_STATE_COUNT = 5;

size_t boot_fill_early_pin_states(EarlyBootPinState* out, size_t capacity);
uint32_t boot_power_stable_delay_ms();
uint32_t boot_tft_reset_low_ms();
uint32_t boot_tft_reset_high_ms();
