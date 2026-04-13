#include <assert.h>

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

    assert(boot_power_stable_delay_ms() >= 100);
    assert(boot_tft_reset_low_ms() >= 10);
    assert(boot_tft_reset_high_ms() >= boot_tft_reset_low_ms());

    return 0;
}
