#include <assert.h>
#include <string.h>

static constexpr size_t kExpectedSerialCapacity = 80;

#include "boot_status.h"

static void fill_pattern(char* buf, size_t size, char start) {
    if (size == 0) {
        return;
    }

    for (size_t i = 0; i < size - 1; ++i) {
        buf[i] = static_cast<char>(start + (i % 26));
    }
    buf[size - 1] = '\0';
}

int main() {
    BootStatus status = boot_status_make();

    boot_status_begin(&status, BootStage::POWER, 12);
    assert(status.stage == BootStage::POWER);
    assert(status.state == BootState::START);
    assert(status.progress_segment == 0);

    boot_status_update(&status, BootState::OK, "rails stable", "rails stable");
    assert(status.state == BootState::OK);
    assert(status.display_detail[0] != '\0');
    assert(strcmp(boot_stage_name(status.stage), "POWER") == 0);
    assert(strcmp(boot_state_name(status.state), "ok") == 0);
    assert(strcmp(boot_stage_name(static_cast<BootStage>(0xFF)), "UNKNOWN") == 0);
    assert(strcmp(boot_state_name(static_cast<BootState>(0xFF)), "unknown") == 0);

    char line[96];
    boot_status_format_line(status, 12, line, sizeof(line));
    const char expected_line[] = "[BOOT][0012 ms][POWER][ok] rails stable";
    assert(strcmp(line, expected_line) == 0);

    for (uint8_t idx = 0; idx < 5; ++idx) {
        boot_status_begin(&status, static_cast<BootStage>(idx), 200 + idx);
        assert(status.progress_segment == idx);
    }

    const BootStage invalid_stage = static_cast<BootStage>(0xFF);
    boot_status_begin(&status, invalid_stage, 300);
    assert(status.progress_segment == 4);
    assert(strcmp(boot_stage_name(status.stage), "UNKNOWN") == 0);

    boot_status_begin(&status, BootStage::GPS, 100);
    assert(strcmp(boot_stage_name(status.stage), "GPS") == 0);

    char long_display[sizeof(status.display_detail) * 2];
    char long_serial[sizeof(status.serial_detail) * 2];
    fill_pattern(long_display, sizeof(long_display), 'A');
    fill_pattern(long_serial, sizeof(long_serial), 'a');
    boot_status_update(&status, BootState::WARN, long_display, long_serial);
    assert(strlen(status.display_detail) == kBootStatusDisplayDetailChars - 1);
    assert(kBootStatusSerialDetailChars == kExpectedSerialCapacity);
    assert(strlen(status.serial_detail) == (kExpectedSerialCapacity - 1));

    const char weird[] = "\xFF\x0A" "bad";
    boot_status_update(&status, BootState::WARN, weird, weird);
    assert(status.display_detail[0] == '?');
    assert(status.serial_detail[0] == '?');

    boot_status_reset(&status);
    assert(status.stage == BootStage::POWER);
    assert(status.state == BootState::START);
    assert(status.stage_started_ms == 0);
    assert(status.display_detail[0] == '\0');
    assert(status.serial_detail[0] == '\0');

    return 0;
}
