#pragma once

#include <stddef.h>

// Lightweight boot logger that buffers lines in RAM until SD becomes
// available, then flushes everything to boot_log.txt in one write.
//
// Usage:
//   boot_log_append("some line");       // before or after SD ready
//   boot_log_flush_to_sd();             // call once after storage_init()

void boot_log_append(const char* line);
void boot_log_flush_to_sd();
