#pragma once

#include <stddef.h>
#include <stdint.h>

void format_lap_time_hundredths(char* buf, size_t len, int32_t time_ms);
void format_delta_hundredths(char* buf, size_t len, int32_t delta_ms);
void format_hhmmss_thousandths(char* buf, size_t len, double total_secs);
