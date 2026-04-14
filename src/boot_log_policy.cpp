#include "boot_log_policy.h"

namespace boot_log_policy {

namespace {

constexpr uint32_t kSpiMutexTimeoutMs = 500;

}  // namespace

uint32_t spi_mutex_timeout_ms() {
    return kSpiMutexTimeoutMs;
}

bool drop_append_on_lock_timeout() {
    return true;
}

bool preserve_buffer_on_flush_lock_timeout() {
    return true;
}

bool should_roll_history(uint32_t file_bytes, uint32_t max_file_bytes) {
    return file_bytes > max_file_bytes;
}

uint32_t history_tail_bytes_to_keep(uint32_t max_file_bytes,
                                    uint32_t pending_append_bytes) {
    if (pending_append_bytes >= max_file_bytes) {
        return 0;
    }
    return max_file_bytes - pending_append_bytes;
}

}  // namespace boot_log_policy
