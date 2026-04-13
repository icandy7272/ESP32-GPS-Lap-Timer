#include "boot_status.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr uint8_t kBootStageCount =
    static_cast<uint8_t>(BootStage::READY) + 1;

inline uint8_t stage_to_segment(BootStage stage) {
    uint8_t idx = static_cast<uint8_t>(stage);
    if (idx >= kBootStageCount) {
        idx = kBootStageCount - 1;
    }
    return idx;
}

size_t sanitize_ascii_copy(char* dest, size_t dest_size, const char* src) {
    if (dest_size == 0 || dest == nullptr) {
        return 0;
    }

    dest[0] = '\0';
    if (src == nullptr) {
        return 0;
    }

    size_t written = 0;
    while (written + 1 < dest_size && *src != '\0') {
        unsigned char c = static_cast<unsigned char>(*src++);
        if (c < 0x20 || c > 0x7E) {
            dest[written++] = '?';
        } else {
            dest[written++] = static_cast<char>(c);
        }
    }

    dest[written] = '\0';
    return written;
}

const char* stage_names[] = {"POWER", "DISPLAY", "STORAGE", "GPS", "READY"};
const char* state_names[] = {"start", "ok", "warn", "fail"};

const char* stage_name_or_default(BootStage stage) {
    const uint8_t idx = static_cast<uint8_t>(stage);
    if (idx < (sizeof(stage_names) / sizeof(stage_names[0]))) {
        return stage_names[idx];
    }
    return "UNKNOWN";
}

const char* state_name_or_default(BootState state) {
    const uint8_t idx = static_cast<uint8_t>(state);
    if (idx < (sizeof(state_names) / sizeof(state_names[0]))) {
        return state_names[idx];
    }
    return "unknown";
}

}  // namespace

BootStatus boot_status_make() {
    BootStatus status;
    status.stage = BootStage::POWER;
    status.state = BootState::START;
    status.stage_started_ms = 0;
    status.progress_segment = stage_to_segment(status.stage);
    status.display_detail[0] = '\0';
    status.serial_detail[0] = '\0';
    return status;
}

void boot_status_reset(BootStatus* status) {
    if (status == nullptr) {
        return;
    }
    *status = boot_status_make();
}

void boot_status_begin(BootStatus* status, BootStage stage, uint32_t now_ms) {
    if (status == nullptr) {
        return;
    }
    status->stage = stage;
    status->state = BootState::START;
    status->stage_started_ms = now_ms;
    status->progress_segment = stage_to_segment(stage);
    status->display_detail[0] = '\0';
    status->serial_detail[0] = '\0';
}

void boot_status_update(BootStatus* status,
                        BootState state,
                        const char* display_detail,
                        const char* serial_detail) {
    if (status == nullptr) {
        return;
    }
    status->state = state;
    sanitize_ascii_copy(status->display_detail, kBootStatusDisplayDetailChars, display_detail);
    sanitize_ascii_copy(status->serial_detail, kBootStatusSerialDetailChars, serial_detail);
}

const char* boot_stage_name(BootStage stage) {
    return stage_name_or_default(stage);
}

const char* boot_state_name(BootState state) {
    return state_name_or_default(state);
}

void boot_status_format_line(const BootStatus& status,
                             uint32_t now_ms,
                             char* out,
                             size_t out_len) {
    if (out == nullptr || out_len == 0) {
        return;
    }
    const char* stage_name = stage_name_or_default(status.stage);
    const char* state_name = state_name_or_default(status.state);
    int written = snprintf(out,
                           out_len,
                           "[BOOT][%04u ms][%s][%s]",
                           static_cast<unsigned>(now_ms),
                           stage_name,
                           state_name);
    if (written < 0) {
        out[0] = '\0';
        return;
    }
    size_t used = static_cast<size_t>(written);
    if (used >= out_len) {
        out[out_len - 1] = '\0';
        return;
    }

    if (status.serial_detail[0] == '\0') {
        return;
    }

    size_t remaining = out_len - used;
    int appended = snprintf(out + used, remaining, " %s", status.serial_detail);
    if (appended < 0) {
        return;
    }
    if (static_cast<size_t>(appended) >= remaining) {
        out[out_len - 1] = '\0';
    }
}
