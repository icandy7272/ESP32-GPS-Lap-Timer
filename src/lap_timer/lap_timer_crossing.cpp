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
    // observed on the 2026-04-17 walking test.
    //
    // The segment is axially extended by CROSSING_END_TOLERANCE_M on
    // each end so a 1-3 m absolute GPS error (either at track-creation
    // point-marking time or during the walk) does not reject a real
    // crossing that grazes past an endpoint.  The 2026-04-18 walk
    // produced exactly this failure mode: closest approach 0.22 m past
    // P2 in recorded coordinates while the walker physically crossed
    // the line.  See docs/TEST_MODES.md for the chosen tolerance value.
    double line_len_m = haversine_m(line->lat1_deg, line->lon1_deg,
                                    line->lat2_deg, line->lon2_deg);
    double ext_fraction = 0.0;
    if (line_len_m > 0.1) {
        ext_fraction = CROSSING_END_TOLERANCE_M / line_len_m;
        // Clamp the extension to at most one segment length per side.
        // Without this, a short detection line (e.g. a 1 m start/finish,
        // which track_creation_has_min_start_finish_separation permits)
        // combined with walking-test's 2 m tolerance would extend each
        // end by 2 m — effectively 5 m total, wider than the intended
        // detection line, and re-introduces the infinite-line false
        // positive the T10 work was meant to close.  Capping the
        // fraction at 1.0 means a 1 m line becomes at most a 3 m
        // effective hitbox: still accommodating, still bounded.
        if (ext_fraction > 1.0) {
            ext_fraction = 1.0;
        }
    }

    if (!line_geometry::segments_intersect_extended(
            prev->lat_deg, prev->lon_deg,
            curr->lat_deg, curr->lon_deg,
            line->lat1_deg, line->lon1_deg,
            line->lat2_deg, line->lon2_deg,
            ext_fraction)) {
        return false;
    }

    float hdiff = heading_diff(curr->heading_deg, line->valid_heading_deg);
    if (fabsf(hdiff) > HEADING_WINDOW) {
        return false;
    }

    return true;
}

// Emit one [xing] `candidate` event per side-flip on a detection line.
//
// This is the structured, live-map-friendly version of the old
// `side_flip hdiff=X REJECTED` message: it fires on EVERY crossing
// candidate (pass or reject) and reports the geometry the live map and
// post-run review tools need to reason about the call.
//
// Format (deliberately machine-parseable, key=val, space-separated):
//
//   [xing] L<idx> candidate u=<f> overshoot=<f> hdiff=<+/-f> \
//          result=<PASS|REJECT> reason=<code>
//
// Reason codes match the roadmap in
// docs/superpowers/plans/2026-04-18-finish-line-live-map-debugging.md:
//   segment                    PASS — crossing projects onto [P1, P2]
//   extension                  PASS — crossing within endpoint tolerance
//   heading_mismatch           REJECT — |heading − valid_heading| > WINDOW
//   outside_endpoint_tolerance REJECT — crossing past the extended segment
//
// The emit order is computed from the same geometric inputs
// has_crossed_line uses, so the `result` field always agrees with the
// downstream accept/reject decision for a given (prev, curr) segment.
static void emit_candidate_event(int line_idx,
                                 const DetectionLine* line,
                                 const GpsPoint* prev,
                                 const GpsPoint* curr,
                                 double line_len_m,
                                 double ext_fraction) {
    // Crossing point by linear interpolation on the prev→curr segment.
    double t = linear_crossing_t(prev, curr, line);
    double xing_lat = prev->lat_deg + t * (curr->lat_deg - prev->lat_deg);
    double xing_lon = prev->lon_deg + t * (curr->lon_deg - prev->lon_deg);

    // Project the crossing onto the finish line (line-local coords).
    // project_to_line works in lat/lon degrees directly: u is unitless,
    // signed_d would be in degrees — we only ever report u + overshoot
    // in metres so the degree-scale signed_d is discarded here.
    double u = 0.0;
    double signed_d_deg = 0.0;
    line_geometry::project_to_line(xing_lat, xing_lon,
                                   line->lat1_deg, line->lon1_deg,
                                   line->lat2_deg, line->lon2_deg,
                                   &u, &signed_d_deg);
    (void)signed_d_deg;  // crossing sits on the line by construction

    // Overshoot in metres: how far past an endpoint the crossing projects.
    // u ∈ [0, 1] ⇒ on the finite segment ⇒ 0 m overshoot.
    double overshoot_m = 0.0;
    if (u < 0.0)       overshoot_m = -u * line_len_m;
    else if (u > 1.0)  overshoot_m = (u - 1.0) * line_len_m;

    float hdiff = heading_diff(curr->heading_deg, line->valid_heading_deg);
    const bool heading_ok   = fabsf(hdiff) <= HEADING_WINDOW;
    // STRICT inequalities match segments_intersect()'s
    // `c1 * c2 >= 0.0 -> no intersection` convention: a crossing point
    // exactly on an endpoint (u == 0.0, 1.0, -ext, or 1+ext) makes one
    // of the two cross products zero, and the segment test rejects.
    // Using closed intervals here would make emit_candidate_event()
    // report PASS for inputs that has_crossed_line() subsequently
    // rejects — the boundary divergence codex flagged in the
    // 2026-04-19 review.  GPS doubles almost never land on the
    // boundary, but `result=` must not lie when they do.
    const bool segment_ok   = (u > 0.0 && u < 1.0);
    const bool extension_ok = (u > -ext_fraction && u < 1.0 + ext_fraction);

    const char* result;
    const char* reason;
    if (!heading_ok) {
        result = "REJECT";
        reason = "heading_mismatch";
    } else if (segment_ok) {
        result = "PASS";
        reason = "segment";
    } else if (extension_ok) {
        result = "PASS";
        reason = "extension";
    } else {
        result = "REJECT";
        reason = "outside_endpoint_tolerance";
    }

    // %+.1f on hdiff: %+.0f rounds 60.4° and 59.6° to the same +60,
    // hiding which side of HEADING_WINDOW (currently 60°) the reject
    // fired on.  One extra decimal disambiguates boundary cases in
    // the live-map candidate panel.  Codex P2 from 2026-04-19.
    Serial.printf(
        "[xing] L%d candidate u=%.3f overshoot=%.2f hdiff=%+.1f "
        "result=%s reason=%s\n",
        line_idx, u, overshoot_m, (double)hdiff, result, reason);
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

    // 2b) Reject low-confidence fixes by HDOP.  A horizontal DOP above
    //     MAX_CROSSING_HDOP typically means satellite geometry is bad
    //     enough that the reported position can drift tens of metres,
    //     which is larger than any finish line is wide.  Crossings at
    //     that noise level are meaningless — dropping the sample here
    //     keeps both the arm state and the debounce logic clean.  The
    //     `hdop > 0` guard skips this when HDOP is unavailable (older
    //     fw without GGA-field-7 fallback, or legacy tests).
    if (curr->hdop > 0.0f && curr->hdop > MAX_CROSSING_HDOP) {
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
        // Structured candidate event for live_map / offline review.
        // Replaces the old `side_flip hdiff=X REJECTED` log line with a
        // full key=val record that also fires on PASS and on geometric
        // rejects (past-endpoint), not only heading rejects.
        double line_len_m = haversine_m(line->lat1_deg, line->lon1_deg,
                                        line->lat2_deg, line->lon2_deg);
        double ext_fraction = 0.0;
        if (line_len_m > 0.1) {
            ext_fraction = CROSSING_END_TOLERANCE_M / line_len_m;
            if (ext_fraction > 1.0) ext_fraction = 1.0;
        }
        emit_candidate_event(line_idx, line, prev, curr,
                             line_len_m, ext_fraction);
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
