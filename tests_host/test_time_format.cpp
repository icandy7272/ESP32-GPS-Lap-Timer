#include "time_format.h"

#include <assert.h>
#include <string.h>

static void test_format_lap_time_hundredths_preserves_tft_resolution() {
    char buf[16] = {0};

    format_lap_time_hundredths(buf, sizeof(buf), 52384);

    assert(strcmp(buf, "0:52.38") == 0);
}

static void test_format_lap_time_hundredths_handles_missing_time() {
    char buf[16] = {0};

    format_lap_time_hundredths(buf, sizeof(buf), -1);

    assert(strcmp(buf, "--:--.--") == 0);
}

static void test_format_lap_time_hundredths_formats_zero_time() {
    char buf[16] = {0};

    format_lap_time_hundredths(buf, sizeof(buf), 0);

    assert(strcmp(buf, "0:00.00") == 0);
}

static void test_format_lap_time_hundredths_formats_exact_minute() {
    char buf[16] = {0};

    format_lap_time_hundredths(buf, sizeof(buf), 60000);

    assert(strcmp(buf, "1:00.00") == 0);
}

static void test_format_delta_hundredths_preserves_sign_and_hundredths() {
    char buf[16] = {0};

    format_delta_hundredths(buf, sizeof(buf), -128);

    assert(strcmp(buf, "-0.12") == 0);
}

static void test_format_delta_hundredths_formats_positive_delta() {
    char buf[16] = {0};

    format_delta_hundredths(buf, sizeof(buf), 128);

    assert(strcmp(buf, "+0.12") == 0);
}

static void test_format_delta_hundredths_formats_zero_delta() {
    char buf[16] = {0};

    format_delta_hundredths(buf, sizeof(buf), 0);

    assert(strcmp(buf, "+0.00") == 0);
}

static void test_format_hhmmss_thousandths_uses_millisecond_resolution() {
    char buf[16] = {0};

    format_hhmmss_thousandths(buf, sizeof(buf), 3723.456);

    assert(strcmp(buf, "010203.456") == 0);
}

static void test_format_hhmmss_thousandths_formats_midnight() {
    char buf[16] = {0};

    format_hhmmss_thousandths(buf, sizeof(buf), 0.0);

    assert(strcmp(buf, "000000.000") == 0);
}

static void test_format_hhmmss_thousandths_clamps_negative_values() {
    char buf[16] = {0};

    format_hhmmss_thousandths(buf, sizeof(buf), -0.4);

    assert(strcmp(buf, "000000.000") == 0);
}

int main() {
    test_format_lap_time_hundredths_preserves_tft_resolution();
    test_format_lap_time_hundredths_handles_missing_time();
    test_format_lap_time_hundredths_formats_zero_time();
    test_format_lap_time_hundredths_formats_exact_minute();
    test_format_delta_hundredths_preserves_sign_and_hundredths();
    test_format_delta_hundredths_formats_positive_delta();
    test_format_delta_hundredths_formats_zero_delta();
    test_format_hhmmss_thousandths_uses_millisecond_resolution();
    test_format_hhmmss_thousandths_formats_midnight();
    test_format_hhmmss_thousandths_clamps_negative_values();
    return 0;
}
