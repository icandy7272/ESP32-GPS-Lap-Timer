#pragma once

#include <stdint.h>

namespace boot_log_policy {

uint32_t spi_mutex_timeout_ms();
bool drop_append_on_lock_timeout();
bool preserve_buffer_on_flush_lock_timeout();
bool should_roll_history(uint32_t file_bytes, uint32_t max_file_bytes);
uint32_t history_tail_bytes_to_keep(uint32_t max_file_bytes,
                                    uint32_t pending_append_bytes);

}  // namespace boot_log_policy
