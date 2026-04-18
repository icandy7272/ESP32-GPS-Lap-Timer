#include "lap_timer_internal.h"

#include "../line_geometry.h"

#include <Arduino.h>
#include <math.h>

namespace lap_timer_internal {

static double catmull_rom_eval(double p0, double p1,
                               double p2, double p3, double u) {
    double u2 = u * u;
    double u3 = u2 * u;
    return 0.5 * ((2.0 * p1)
                 + (-p0 + p2) * u
                 + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * u2
                 + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * u3);
}

static void spline_position(const GpsPoint* p0, const GpsPoint* p1,
                            const GpsPoint* p2, const GpsPoint* p3,
                            double u, double* out_lat, double* out_lon) {
    *out_lat = catmull_rom_eval(p0->lat_deg, p1->lat_deg,
                                p2->lat_deg, p3->lat_deg, u);
    *out_lon = catmull_rom_eval(p0->lon_deg, p1->lon_deg,
                                p2->lon_deg, p3->lon_deg, u);
}

static double side_of_line(double plat, double plon,
                           const DetectionLine* line) {
    double lx = line->lat2_deg - line->lat1_deg;
    double ly = line->lon2_deg - line->lon1_deg;
    double px = plat - line->lat1_deg;
    double py = plon - line->lon1_deg;
    return cross_product_2d(lx, ly, px, py);
}

static double spline_find_crossing(const GpsPoint* p0, const GpsPoint* p1,
                                   const GpsPoint* p2, const GpsPoint* p3,
                                   const DetectionLine* line) {
    double lo = 0.0;
    double hi = 1.0;
    double sign_lo;
    {
        double lat;
        double lon;
        spline_position(p0, p1, p2, p3, lo, &lat, &lon);
        sign_lo = side_of_line(lat, lon, line);
    }

    for (int i = 0; i < BINARY_SEARCH_ITS; i++) {
        double mid = (lo + hi) * 0.5;
        double lat;
        double lon;
        spline_position(p0, p1, p2, p3, mid, &lat, &lon);
        double sign_mid = side_of_line(lat, lon, line);

        if ((sign_lo > 0.0) == (sign_mid > 0.0)) {
            lo = mid;
            sign_lo = sign_mid;
        } else {
            hi = mid;
        }
    }

    return (lo + hi) * 0.5;
}

static double linear_crossing_t(const GpsPoint* prev, const GpsPoint* curr,
                                const DetectionLine* line) {
    double s_prev = side_of_line(prev->lat_deg, prev->lon_deg, line);
    double s_curr = side_of_line(curr->lat_deg, curr->lon_deg, line);
    double denom = s_curr - s_prev;
    if (fabs(denom) < 1e-15) {
        return 0.5;
    }

    double t = -s_prev / denom;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return t;
}

static int64_t compute_crossing_time(const DetectionLine* line) {
    if (s_history_count >= SPLINE_HISTORY) {
        const GpsPoint* p0 = history_get(1);
        const GpsPoint* p1 = history_get(2);
        const GpsPoint* p2 = history_get(3);
        const GpsPoint* p3 = history_get(3);
        double t = spline_find_crossing(p0, p1, p2, p3, line);
        int64_t dt = p2->timestamp_us - p1->timestamp_us;
        return p1->timestamp_us + (int64_t)(t * (double)dt);
    }

    const GpsPoint* prev = history_get(s_history_count - 2);
    const GpsPoint* curr = history_get(s_history_count - 1);
    double t = linear_crossing_t(prev, curr, line);
    int64_t dt = curr->timestamp_us - prev->timestamp_us;
    return prev->timestamp_us + (int64_t)(t * (double)dt);
}

static bool has_crossed_line(const GpsPoint* prev, const GpsPoint* curr,
                             const DetectionLine* line) {
    // Require an actual segment-segment intersection, not just a sign
    // flip on the infinite line through P1-P2.  The old
    // side_of_line-only check treated walks along the line's EXTENSION
    // as crossings, which produced the 63.6s → 4.7s false lap pair
    // observed on the 2026-04-18 walking test.
    // See src/line_geometry.cpp for the pure geometry predicate and
    // tests_host/test_line_geometry.cpp for the regression coverage.
    if (!line_geometry::segments_intersect(
            prev->lat_deg, prev->lon_deg,
            curr->lat_deg, curr->lon_deg,
            line->lat1_deg, line->lon1_deg,
            line->lat2_deg, line->lon2_deg)) {
        return false;
    }

    float hdiff = heading_diff(curr->heading_deg, line->valid_heading_deg);
    if (fabsf(hdiff) > HEADING_WINDOW) {
        return false;
    }

    return true;
}

static void update_arm_distance(int line_idx,
                                const GpsPoint* prev,
                                const GpsPoint* curr) {
    double dist = haversine_m(prev->lat_deg, prev->lon_deg,
                              curr->lat_deg, curr->lon_deg);
    s_arm_distance[line_idx] += dist;
    if (s_arm_distance[line_idx] > ARM_DISTANCE_M && !s_arm_ready[line_idx]) {
        s_arm_ready[line_idx] = true;
        Serial.printf("[xing] L%d ARMED at %.1fm\n",
                      line_idx, s_arm_distance[line_idx]);
    }
}

static void reset_arm(int line_idx) {
    s_arm_distance[line_idx] = 0.0;
    s_arm_ready[line_idx] = false;
}

static void debounce_start(int line_idx, int64_t crossing_us,
                           double crossed_side_sign) {
    s_debounce_active[line_idx] = true;
    s_debounce_remaining[line_idx] = DEBOUNCE_SAMPLES;
    s_debounce_crossing_us[line_idx] = crossing_us;
    s_debounce_expected_sign[line_idx] = crossed_side_sign;
}

static bool debounce_feed(int line_idx,
                          const GpsPoint* curr,
                          const DetectionLine* line) {
    if (!s_debounce_active[line_idx]) {
        return false;
    }

    double current_sign = side_of_line(curr->lat_deg, curr->lon_deg, line);
    bool same_side = (current_sign * s_debounce_expected_sign[line_idx]) > 0;

    if (!same_side) {
        s_debounce_active[line_idx] = false;
        return false;
    }

    s_debounce_remaining[line_idx]--;
    if (s_debounce_remaining[line_idx] <= 0) {
        s_debounce_active[line_idx] = false;
        return true;
    }
    return false;
}

void process_line(int line_idx,
                  const DetectionLine* line,
                  const GpsPoint* prev,
                  const GpsPoint* curr) {
    // 1) Let any in-progress debounce complete regardless of current speed.
    //    A crossing detected at >1 km/h must be allowed to confirm even if
    //    the driver slows / stops immediately after.  Otherwise the line
    //    gets stuck in debounce_active forever and we miss a real lap.
    if (s_debounce_active[line_idx]) {
        if (debounce_feed(line_idx, curr, line)) {
            int64_t crossing_us = s_debounce_crossing_us[line_idx];
            reset_arm(line_idx);
            Serial.printf("[xing] L%d CROSSED arm=%.1f\n",
                          line_idx, s_arm_distance[line_idx]);

            if (line_idx == 0) {
                handle_finish_crossing(crossing_us);
            } else {
                handle_sector_crossing(line_idx, crossing_us);
            }
        }
        return;
    }

    // 2) Reject stationary GPS drift from arming the line and from
    //    triggering fresh crossings.  u-blox RMC reports ~0 km/h when
    //    stationary, so any real walking/driving crosses the
    //    MIN_CROSSING_SPEED_KMH gate.  Placed AFTER the debounce branch
    //    so in-flight debounces can still complete below the threshold.
    if (curr->speed_kmh < MIN_CROSSING_SPEED_KMH) {
        return;
    }

    update_arm_distance(line_idx, prev, curr);

    if (!s_arm_ready[line_idx]) {
        return;
    }

    double s_prev_val = side_of_line(prev->lat_deg, prev->lon_deg, line);
    double s_curr_val = side_of_line(curr->lat_deg, curr->lon_deg, line);
    bool side_changed = (s_prev_val > 0.0) != (s_curr_val > 0.0);

    if (side_changed) {
        float hdiff = heading_diff(curr->heading_deg, line->valid_heading_deg);
        if (fabsf(hdiff) > HEADING_WINDOW) {
            Serial.printf("[xing] L%d side_flip hdiff=%.0f REJECTED\n",
                          line_idx, hdiff);
        }
    }

    if (!has_crossed_line(prev, curr, line)) {
        return;
    }

    int64_t crossing_us = compute_crossing_time(line);
    double crossed_side = side_of_line(curr->lat_deg, curr->lon_deg, line);
    Serial.printf("[xing] L%d debounce_start side=%.2e\n",
                  line_idx, crossed_side);
    debounce_start(line_idx, crossing_us, crossed_side);
}

}  // namespace lap_timer_internal
