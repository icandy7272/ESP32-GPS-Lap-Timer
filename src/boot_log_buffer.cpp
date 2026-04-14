#include "boot_log_buffer.h"

#include <string.h>

namespace boot_log_buffer {

namespace {

void note_overflow(State* state) {
    if (state == nullptr || state->overflowed) {
        return;
    }

    const size_t marker_len = strlen(kOverflowMarker);
    if (marker_len >= kBufferSize) {
        state->size = 0;
        state->overflowed = true;
        return;
    }

    const size_t marker_start = kBufferSize - marker_len;
    if (state->size > marker_start) {
        state->size = marker_start;
    }

    memcpy(state->data + marker_start, kOverflowMarker, marker_len);
    state->size = kBufferSize;
    state->overflowed = true;
}

}  // namespace

void append(State* state, const char* text, size_t len) {
    if (state == nullptr || text == nullptr || len == 0 || state->overflowed) {
        return;
    }

    if (state->size + len > kBufferSize) {
        note_overflow(state);
        return;
    }

    memcpy(state->data + state->size, text, len);
    state->size += len;
}

void append_line(State* state, const char* line) {
    if (state == nullptr || line == nullptr) {
        return;
    }

    append(state, line, strlen(line));
    append(state, "\n", 1);
}

void finish_flush(State* state, bool success) {
    if (state == nullptr || !success) {
        return;
    }

    state->size = 0;
    state->overflowed = false;
    state->sd_ready = true;
}

}  // namespace boot_log_buffer
