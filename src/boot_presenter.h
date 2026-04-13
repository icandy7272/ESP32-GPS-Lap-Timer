#pragma once

#include <stdint.h>

#include "boot_status.h"

struct BootView {
    // String pointers are borrowed from the presenter input; the caller retains ownership.
    const char* stage_label;
    // detail_line is either BootStatus::display_detail or a static fallback.
    const char* detail_line;
    uint8_t completed_segments;
    uint8_t active_segment;
    bool fatal;
};

BootView boot_present(const BootStatus& status);
