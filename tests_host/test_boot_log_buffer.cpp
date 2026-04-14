#include <assert.h>
#include <string.h>

#include "boot_log_buffer.h"

int main() {
    boot_log_buffer::State state{};

    boot_log_buffer::append_line(&state, "boot start");
    assert(state.size == strlen("boot start\n"));
    assert(memcmp(state.data, "boot start\n", state.size) == 0);
    assert(!state.overflowed);
    assert(!state.sd_ready);

    boot_log_buffer::State overflow_state{};
    const size_t marker_len = strlen(boot_log_buffer::kOverflowMarker);
    const size_t fill_len = boot_log_buffer::kBufferSize;
    for (size_t i = 0; i < fill_len; ++i) {
        boot_log_buffer::append(&overflow_state, "A", 1);
    }
    boot_log_buffer::append(&overflow_state, "B", 1);

    assert(overflow_state.size == boot_log_buffer::kBufferSize);
    assert(overflow_state.overflowed);
    assert(memcmp(overflow_state.data + boot_log_buffer::kBufferSize - marker_len,
                  boot_log_buffer::kOverflowMarker,
                  marker_len) == 0);

    boot_log_buffer::append_line(&overflow_state, "later line");
    assert(overflow_state.size == boot_log_buffer::kBufferSize);

    boot_log_buffer::State flush_state{};
    boot_log_buffer::append_line(&flush_state, "before flush");
    const size_t buffered_size = flush_state.size;

    boot_log_buffer::finish_flush(&flush_state, false);
    assert(flush_state.size == buffered_size);
    assert(!flush_state.sd_ready);

    boot_log_buffer::finish_flush(&flush_state, true);
    assert(flush_state.size == 0);
    assert(!flush_state.overflowed);
    assert(flush_state.sd_ready);

    return 0;
}
