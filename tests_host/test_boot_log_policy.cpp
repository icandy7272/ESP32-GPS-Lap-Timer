#include <assert.h>

#include "boot_log_policy.h"

int main() {
    assert(boot_log_policy::spi_mutex_timeout_ms() > 0);
    assert(boot_log_policy::spi_mutex_timeout_ms() <= 1000);
    assert(boot_log_policy::drop_append_on_lock_timeout());
    assert(boot_log_policy::preserve_buffer_on_flush_lock_timeout());
    assert(!boot_log_policy::should_roll_history(32768, 32768));
    assert(boot_log_policy::should_roll_history(32769, 32768));
    assert(boot_log_policy::history_tail_bytes_to_keep(32768, 1024) == 31744);
    assert(boot_log_policy::history_tail_bytes_to_keep(32768, 4096) == 28672);
    assert(boot_log_policy::history_tail_bytes_to_keep(32768, 32768) == 0);
    assert(boot_log_policy::history_tail_bytes_to_keep(32768, 40000) == 0);
    return 0;
}
