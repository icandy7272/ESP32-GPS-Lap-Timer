#include "time_format.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void format_lap_time_hundredths(char* buf, size_t len, int32_t time_ms) {
    if (time_ms < 0) {
        snprintf(buf, len, "--:--.--");
        return;
    }

    int mins = time_ms / 60000;
    int secs = (time_ms % 60000) / 1000;
    int hundredths = (time_ms % 1000) / 10;
    snprintf(buf, len, "%d:%02d.%02d", mins, secs, hundredths);
}

void format_delta_hundredths(char* buf, size_t len, int32_t delta_ms) {
    const char sign = (delta_ms >= 0) ? '+' : '-';
    int abs_ms = abs(delta_ms);
    int secs = abs_ms / 1000;
    int hundredths = (abs_ms % 1000) / 10;
    snprintf(buf, len, "%c%d.%02d", sign, secs, hundredths);
}

void format_hhmmss_thousandths(char* buf, size_t len, double total_secs) {
    long long total_ms = llround(total_secs * 1000.0);
    if (total_ms < 0) {
        total_ms = 0;
    }

    long long total_int_secs = total_ms / 1000;
    int millis = (int)(total_ms % 1000);
    int hours = (int)(total_int_secs / 3600);
    int minutes = (int)((total_int_secs % 3600) / 60);
    int seconds = (int)(total_int_secs % 60);

    snprintf(buf, len, "%02d%02d%02d.%03d",
             hours, minutes, seconds, millis);
}
