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

enum class BootResetReason : uint8_t {
    UNKNOWN = 0,
    POWERON = 1,
    EXT = 2,
    SW = 3,
    PANIC = 4,
    INT_WDT = 5,
    TASK_WDT = 6,
    WDT = 7,
    DEEPSLEEP = 8,
    BROWNOUT = 9,
    SDIO = 10,
    OTHER = 0xFF,
};

size_t boot_fill_early_pin_states(EarlyBootPinState* out, size_t capacity);
uint32_t boot_warm_power_stable_delay_ms();
uint32_t boot_cold_power_stable_delay_ms();
uint32_t boot_tft_reset_low_ms();
uint32_t boot_tft_reset_high_ms();
BootResetReason boot_reset_reason_from_raw(uint32_t raw_reason);
const char* boot_reset_reason_short(BootResetReason reason);
const char* boot_reset_reason_short(uint32_t raw_reason);
const char* boot_reset_reason_detail(BootResetReason reason);
const char* boot_reset_reason_detail(uint32_t raw_reason);
bool boot_reset_reason_is_crash(BootResetReason reason);
bool boot_reset_reason_is_crash(uint32_t raw_reason);
const char* boot_probe_name(EarlyBootProbe probe);
const char* boot_probe_name(uint8_t raw_probe);
void boot_probe_format(uint8_t raw_probe, char* out, size_t out_len);
