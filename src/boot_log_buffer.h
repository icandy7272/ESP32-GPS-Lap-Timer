#pragma once

#include <stddef.h>

namespace boot_log_buffer {

constexpr size_t kBufferSize = 4096;
constexpr char kOverflowMarker[] = "\n[boot_log: buffer overflow, lines dropped]\n";

struct State {
    char data[kBufferSize]{};
    size_t size = 0;
    bool overflowed = false;
    bool sd_ready = false;
};

void append(State* state, const char* text, size_t len);
void append_line(State* state, const char* line);
void finish_flush(State* state, bool success);

}  // namespace boot_log_buffer
