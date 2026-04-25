#include "crossing_time.h"

#include <assert.h>

static GpsPoint make_point(double lat_deg,
                           double lon_deg,
                           int64_t timestamp_us) {
    GpsPoint point = {};
    point.lat_deg = lat_deg;
    point.lon_deg = lon_deg;
    point.timestamp_us = timestamp_us;
    point.fix_3d = true;
    point.satellites = 12;
    return point;
}

static DetectionLine north_south_line() {
    DetectionLine line = {};
    line.lat1_deg = 0.0;
    line.lon1_deg = 0.0;
    line.lat2_deg = 1.0;
    line.lon2_deg = 0.0;
    line.valid_heading_deg = 0.0f;
    return line;
}

static void test_linear_crossing_uses_signed_distance_fraction() {
    DetectionLine line = north_south_line();
    GpsPoint prev = make_point(0.0, -0.10, 1000000);
    GpsPoint curr = make_point(0.0, 0.30, 1040000);

    int64_t crossing_us = crossing_time_linear_us(&prev, &curr, &line);

    assert(crossing_us == 1010000);
}

static void test_centered_crossing_uses_future_point() {
    DetectionLine line = north_south_line();
    GpsPoint p0 = make_point(0.0, -0.20, 960000);
    GpsPoint p1 = make_point(0.0, -0.05, 1000000);
    GpsPoint p2 = make_point(0.0, 0.05, 1040000);
    GpsPoint p3_fast = make_point(0.0, 0.50, 1080000);
    GpsPoint p3_dup = p2;

    int64_t centered_us =
        crossing_time_centered_us(&p0, &p1, &p2, &p3_fast, &line);
    int64_t duplicated_tail_us =
        crossing_time_centered_us(&p0, &p1, &p2, &p3_dup, &line);

    assert(centered_us >= p1.timestamp_us);
    assert(centered_us <= p2.timestamp_us);
    assert(duplicated_tail_us >= p1.timestamp_us);
    assert(duplicated_tail_us <= p2.timestamp_us);
    assert(centered_us != duplicated_tail_us);
}

static void test_centered_crossing_falls_back_when_time_not_monotonic() {
    DetectionLine line = north_south_line();
    GpsPoint p0 = make_point(0.0, -0.20, 960000);
    GpsPoint p1 = make_point(0.0, -0.10, 1040000);
    GpsPoint p2 = make_point(0.0, 0.30, 1000000);
    GpsPoint p3 = make_point(0.0, 0.50, 1080000);

    int64_t crossing_us =
        crossing_time_centered_us(&p0, &p1, &p2, &p3, &line);

    assert(crossing_us == crossing_time_linear_us(&p1, &p2, &line));
}

static void test_centered_crossing_falls_back_when_p0_or_p3_null() {
    DetectionLine line = north_south_line();
    GpsPoint p1 = make_point(0.0, -0.10, 1000000);
    GpsPoint p2 = make_point(0.0, 0.30, 1040000);
    int64_t baseline = crossing_time_linear_us(&p1, &p2, &line);

    assert(crossing_time_centered_us(nullptr, &p1, &p2, &p2, &line)
           == baseline);
    assert(crossing_time_centered_us(&p1, &p1, &p2, nullptr, &line)
           == baseline);
}

int main() {
    test_linear_crossing_uses_signed_distance_fraction();
    test_centered_crossing_uses_future_point();
    test_centered_crossing_falls_back_when_time_not_monotonic();
    test_centered_crossing_falls_back_when_p0_or_p3_null();
    return 0;
}
