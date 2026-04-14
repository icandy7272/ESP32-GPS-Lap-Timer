#include <assert.h>
#include <string.h>

#include "boot_sequence.h"
#include "pins.h"

int main() {
    EarlyBootPinState states[EARLY_BOOT_PIN_STATE_COUNT] = {};
    size_t count = boot_fill_early_pin_states(states, EARLY_BOOT_PIN_STATE_COUNT);

    assert(count == EARLY_BOOT_PIN_STATE_COUNT);

    assert(states[0].pin == PIN_SD_CS);
    assert(states[0].level_high);

    assert(states[1].pin == PIN_TFT_CS);
    assert(states[1].level_high);

    assert(states[2].pin == PIN_TFT_RST);
    assert(!states[2].level_high);

    assert(states[3].pin == PIN_TFT_DC);
    assert(states[3].level_high);

    assert(states[4].pin == PIN_TFT_BL);
    assert(!states[4].level_high);

    assert(boot_warm_power_stable_delay_ms() >= 500);
    assert(boot_cold_power_stable_delay_ms() >= 1500);
    assert(boot_cold_power_stable_delay_ms() > boot_warm_power_stable_delay_ms());
    assert(boot_tft_reset_low_ms() >= 10);
    assert(boot_tft_reset_high_ms() >= boot_tft_reset_low_ms());

    assert(strcmp(boot_probe_name(EarlyBootProbe::SETUP_ENTRY), "SETUP_ENTRY") == 0);
    assert(strcmp(boot_probe_name(EarlyBootProbe::DISPLAY_READY), "DISPLAY_READY") == 0);
    assert(strcmp(boot_probe_name(static_cast<uint8_t>(EarlyBootProbe::READY)), "READY") == 0);
    assert(strcmp(boot_probe_name(static_cast<uint8_t>(0x42)), "UNKNOWN") == 0);

    char probe[32];
    boot_probe_format(static_cast<uint8_t>(EarlyBootProbe::STORAGE_START),
                      probe,
                      sizeof(probe));
    assert(strcmp(probe, "STORAGE_START") == 0);

    boot_probe_format(kBootProbeCleared, probe, sizeof(probe));
    assert(strcmp(probe, "CLEARED") == 0);

    boot_probe_format(0x42, probe, sizeof(probe));
    assert(strcmp(probe, "UNKNOWN(0x42)") == 0);

    assert(boot_reset_reason_from_raw(1) == BootResetReason::POWERON);
    assert(boot_reset_reason_from_raw(4) == BootResetReason::PANIC);
    assert(boot_reset_reason_from_raw(9) == BootResetReason::BROWNOUT);
    assert(boot_reset_reason_from_raw(99) == BootResetReason::OTHER);

    assert(strcmp(boot_reset_reason_short(BootResetReason::POWERON), "POWERON") == 0);
    assert(strcmp(boot_reset_reason_short(BootResetReason::INT_WDT), "INT_WDT") == 0);
    assert(strcmp(boot_reset_reason_short(BootResetReason::OTHER), "OTHER") == 0);

    assert(strcmp(boot_reset_reason_detail(BootResetReason::PANIC),
                  "PANIC (Guru Meditation)") == 0);
    assert(strcmp(boot_reset_reason_detail(BootResetReason::BROWNOUT),
                  "BROWNOUT (voltage drop)") == 0);
    assert(strcmp(boot_reset_reason_detail(BootResetReason::SW),
                  "SW (esp_restart)") == 0);

    assert(boot_reset_reason_is_crash(BootResetReason::PANIC));
    assert(boot_reset_reason_is_crash(BootResetReason::TASK_WDT));
    assert(!boot_reset_reason_is_crash(BootResetReason::POWERON));
    assert(!boot_reset_reason_is_crash(BootResetReason::SW));

    return 0;
}
