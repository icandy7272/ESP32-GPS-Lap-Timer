#pragma once

#include <stddef.h>
#include <stdint.h>

struct EarlyBootPinState {
    int  pin;
    bool level_high;
};

constexpr size_t EARLY_BOOT_PIN_STATE_COUNT = 5;
constexpr uint8_t kBootProbeCleared = 0xFF;

enum class EarlyBootProbe : uint8_t {
    SETUP_ENTRY = 0,
    SERIAL_READY,
    POWER_STABLE,
    DISPLAY_START,
    DISPLAY_READY,
    STORAGE_START,
    STORAGE_READY,
    GPS_START,
    READY,
};

size_t boot_fill_early_pin_states(EarlyBootPinState* out, size_t capacity);
uint32_t boot_power_stable_delay_ms();
uint32_t boot_tft_reset_low_ms();
uint32_t boot_tft_reset_high_ms();
const char* boot_probe_name(EarlyBootProbe probe);
const char* boot_probe_name(uint8_t raw_probe);
void boot_probe_format(uint8_t raw_probe, char* out, size_t out_len);
