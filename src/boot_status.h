#pragma once

#include <stddef.h>
#include <stdint.h>

enum class BootStage : uint8_t { POWER = 0, DISPLAY_STAGE, STORAGE, GPS, READY };
enum class BootState : uint8_t { START = 0, OK, WARN, FAIL };

constexpr size_t kBootStatusDisplayDetailChars = 48;
constexpr size_t kBootStatusSerialDetailChars = 80;

struct BootStatus {
    BootStage stage;
    BootState state;
    char display_detail[kBootStatusDisplayDetailChars];
    char serial_detail[kBootStatusSerialDetailChars];
    uint32_t stage_started_ms;
    // Always clamped to the five coarse stages (0..4).
    uint8_t progress_segment;
};

BootStatus boot_status_make();
void boot_status_reset(BootStatus* status);
void boot_status_begin(BootStatus* status, BootStage stage, uint32_t now_ms);
void boot_status_update(BootStatus* status,
                        BootState state,
                        const char* display_detail,
                        const char* serial_detail);
const char* boot_stage_name(BootStage stage);
const char* boot_state_name(BootState state);
// now_ms is boot-relative milliseconds.
void boot_status_format_line(const BootStatus& status,
                             uint32_t now_ms,
                             char* out,
                             size_t out_len);
