// Host tests for line_geometry::segments_intersect.
//
// The motivating scenario is the 2026-04-18 walking test, where the
// lap_timer treated the INFINITE extension of the start/finish line as
// the detection surface.  Walking a small circular loop near (but not
// across) the actual P1-P2 segment produced a 63.6s → 4.7s lap pair —
// the crossing point computed in the infinite-line extension fell
// outside [P1, P2], yet was still counted as a lap crossing.
//
// These tests pin down the segment-segment semantics directly.

#include "line_geometry.h"

#include <assert.h>
#include <cmath>
#include <stdio.h>

using line_geometry::segments_intersect;

static int fail_count = 0;

static void check(const char* desc, bool got, bool want) {
    if (got != want) {
        fprintf(stderr, "FAIL: %s — got %d, want %d\n",
                desc, got ? 1 : 0, want ? 1 : 0);
        fail_count++;
    }
}

int main() {
    // --- Clear crossing ---
    // Horizontal line from (-1,0) to (1,0); path from (0,-1) to (0,1).
    // Paths cross at origin, both endpoints on opposite sides.
    check("perpendicular cross at origin",
          segments_intersect(0.0, -1.0, 0.0, 1.0,
                             -1.0, 0.0, 1.0, 0.0),
          true);

    // --- No crossing, parallel ---
    check("parallel horizontal segments",
          segments_intersect(0.0, 0.0, 1.0, 0.0,
                             0.0, 1.0, 1.0, 1.0),
          false);

    // --- 2026-04-18 regression: path crosses infinite extension,
    //     not the actual segment.
    // Detection line: (0,0) to (0,10) — a vertical segment of length 10.
    // User path far to the north, crossing the line's Y-extension:
    // (-1, 50) to (1, 50).  Infinite-line test would pass (path goes
    // through x=0, i.e. through the line's infinite y-axis), but the
    // segment-segment test must reject because the crossing y=50 is
    // outside [0, 10].
    check("regression: crosses infinite extension far from segment",
          segments_intersect(-1.0, 50.0, 1.0, 50.0,
                             0.0, 0.0, 0.0, 10.0),
          false);

    // Same geometry but path crosses within the segment Y-range:
    check("perpendicular cross inside segment range",
          segments_intersect(-1.0, 5.0, 1.0, 5.0,
                             0.0, 0.0, 0.0, 10.0),
          true);

    // --- T-junction: path ends exactly on the detection segment.
    // Endpoint-on-line is undefined; our implementation returns false.
    // Documented here so refactors don't silently flip it without tests.
    check("path endpoint exactly on detection segment (degenerate)",
          segments_intersect(-1.0, 5.0, 0.0, 5.0,
                             0.0, 0.0, 0.0, 10.0),
          false);

    // --- Endpoints on opposite sides of detection line but segments
    //     miss each other in the other axis (non-crossing skew).
    // Path: (0, 0) → (10, 1)
    // Line: (5, 100) → (5, 200)
    // Path crosses x=5 at y≈0.5, well outside [100, 200].
    check("skew — path x-crosses where line is absent",
          segments_intersect(0.0, 0.0, 10.0, 1.0,
                             5.0, 100.0, 5.0, 200.0),
          false);

    // --- Diagonal crossing ---
    check("diagonal crossing",
          segments_intersect(0.0, 0.0, 10.0, 10.0,
                             0.0, 10.0, 10.0, 0.0),
          true);

    // --- Touching endpoints only (shared corner) — undefined / false
    //     for the same reason as T-junction.
    check("segments share a corner",
          segments_intersect(0.0, 0.0, 5.0, 0.0,
                             5.0, 0.0, 5.0, 5.0),
          false);

    // --- Realistic lap-timer geometry: the 2026-04-18 track "test".
    // Start/finish segment from (40.0059785, 116.4584973) to
    // (40.0058567, 116.4585635) — ~14.7 m long, roughly SE bearing.
    // Axes here: x = lat (degrees), y = lon (degrees).
    const double p1_lat = 40.0059785, p1_lon = 116.4584973;
    const double p2_lat = 40.0058567, p2_lon = 116.4585635;

    // (a) User actually walks across the line segment (prev N of P1,
    //     curr S of P2, both reasonably close to the line).  Use a
    //     small ~3 m step either side of the segment midpoint.
    {
        const double mid_lat = (p1_lat + p2_lat) / 2.0;
        const double mid_lon = (p1_lon + p2_lon) / 2.0;
        // Step WNW and ESE by ~2 m each (1 deg lat ≈ 111 km).
        const double step_lat = 2.0 / 111000.0;
        const double step_lon = 2.0 / (111000.0 * 0.766); // cos(40°)
        check("track test: real crossing at midpoint",
              segments_intersect(mid_lat + step_lat, mid_lon - step_lon,
                                 mid_lat - step_lat, mid_lon + step_lon,
                                 p1_lat, p1_lon, p2_lat, p2_lon),
              true);
    }

    // (b) Walking loop ~50 m north of the segment that incidentally
    //     crosses the INFINITE EXTENSION of the P1-P2 line.  This is
    //     the exact scenario the 2026-04-18 session tripped on.
    {
        // Move start position 50 m north; compute path across the line's
        // extension direction.
        const double dx = p2_lat - p1_lat; // lat delta (north-south)
        const double dy = p2_lon - p1_lon; // lon delta
        // Normal to the line (rotate 90°).
        const double nlat = -dy;
        const double nlon = dx;
        const double inv_len = 1.0 / std::sqrt(nlat * nlat + nlon * nlon);
        const double n_lat_u = nlat * inv_len;
        const double n_lon_u = nlon * inv_len;

        // Sample point at P1 + 50m normal.
        const double offset_deg = 50.0 / 111000.0;
        const double far_lat = p1_lat + n_lat_u * offset_deg;
        const double far_lon = p1_lon + n_lon_u * offset_deg;
        // Walk a 4 m chord across the infinite extension direction.
        const double along_lat = dx * 0.5 * inv_len * (4.0 / 111000.0);
        const double along_lon = dy * 0.5 * inv_len * (4.0 / 111000.0);

        check("track test: walk 50m off, crosses infinite extension (regression)",
              segments_intersect(far_lat - along_lat, far_lon - along_lon,
                                 far_lat + along_lat, far_lon + along_lon,
                                 p1_lat, p1_lon, p2_lat, p2_lon),
              false);
    }

    // =========================================================
    // segments_intersect_extended: axial end tolerance coverage
    // =========================================================
    using line_geometry::segments_intersect_extended;

    // Zero tolerance is equivalent to strict segments_intersect.
    check("extended: zero fraction matches strict (miss)",
          segments_intersect_extended(-1.0, 50.0, 1.0, 50.0,
                                      0.0, 0.0, 0.0, 10.0,
                                      0.0),
          false);
    check("extended: zero fraction matches strict (hit)",
          segments_intersect_extended(-1.0, 5.0, 1.0, 5.0,
                                      0.0, 0.0, 0.0, 10.0,
                                      0.0),
          true);
    check("extended: negative fraction treated as zero",
          segments_intersect_extended(-1.0, 50.0, 1.0, 50.0,
                                      0.0, 0.0, 0.0, 10.0,
                                      -0.5),
          false);

    // Axial end-tolerance absorbs a small overshoot past the P2 endpoint.
    // Line y=[0,10] on x=0; path crosses x=0 at y=11 (1 unit past P2).
    // With 20 % extension (line length 10 → 2-unit extension) the
    // crossing sits inside the extended segment [-2, 12].
    check("extended: 1 unit past P2, 20% fraction (should pass)",
          segments_intersect_extended(-1.0, 11.0, 1.0, 11.0,
                                      0.0, 0.0, 0.0, 10.0,
                                      0.20),
          true);
    // 5 % extension only gives 0.5-unit tolerance — 1 unit past is still
    // outside the extended segment [-0.5, 10.5].
    check("extended: 1 unit past P2, 5% fraction (should still fail)",
          segments_intersect_extended(-1.0, 11.0, 1.0, 11.0,
                                      0.0, 0.0, 0.0, 10.0,
                                      0.05),
          false);

    // Tolerance extends BOTH ends.  Path 1.5 units BEFORE P1 with
    // 20 % extension (2-unit tolerance) should pass.
    check("extended: 1.5 units before P1, 20% fraction (should pass)",
          segments_intersect_extended(-1.0, -1.5, 1.0, -1.5,
                                      0.0, 0.0, 0.0, 10.0,
                                      0.20),
          true);

    // Excessive fraction documents pure-function behaviour: the helper
    // does NOT clamp internally.  Callers whose tolerance can exceed
    // the segment length (e.g. lap_timer has_crossed_line on a 1 m
    // detection line with a 2 m walker tolerance) MUST clamp the
    // fraction themselves, otherwise a 10-unit line with fraction 2.0
    // becomes a 50-unit effective hitbox and re-opens the
    // infinite-line false-positive regression.
    //   Line [0,0]..[0,10] extended by fraction 2.0 on each end covers
    //   y ∈ [-20, 30].  A path at y = 25 (15 past the original end)
    //   must therefore intersect.
    check("extended: fraction=2.0 extends 2x line length each end",
          segments_intersect_extended(-1.0, 25.0, 1.0, 25.0,
                                      0.0, 0.0, 0.0, 10.0,
                                      2.0),
          true);
    //   Same geometry with fraction 1.0 (the cap lap_timer applies)
    //   covers y ∈ [-10, 20] — y = 25 must be rejected.
    check("extended: fraction=1.0 caps extension at one line length",
          segments_intersect_extended(-1.0, 25.0, 1.0, 25.0,
                                      0.0, 0.0, 0.0, 10.0,
                                      1.0),
          false);

    // 2026-04-18 WALK REGRESSION, recreated in this coordinate system.
    // Walker's recorded path came within 0.22 m of the P2 endpoint of a
    // 10.4 m line with the recorded coordinates showing no intersection.
    // Using the firmware's 2 m tolerance on a 10.4 m line gives
    // fraction ≈ 0.192.  A path 0.22 m past P2 with that fraction must
    // now register as a crossing.
    {
        const double line_len = 10.4;  // metres in local toy units
        const double tolerance_m = 2.0;
        const double frac = tolerance_m / line_len;
        // Line from (0,0) to (0, line_len).  Path crosses x=0 at
        // y = line_len + 0.22 (0.22 m past P2).
        const double y_past = line_len + 0.22;
        check("extended: 2026-04-18 0.22m past P2 with 2m tolerance",
              segments_intersect_extended(-1.0, y_past, 1.0, y_past,
                                          0.0, 0.0, 0.0, line_len,
                                          frac),
              true);
        // Same geometry with only 0.1 m tolerance (fraction ≈ 0.0096)
        // must still reject — a real track mis-alignment should not be
        // masked by an aggressive tolerance setting.
        const double frac_tight = 0.1 / line_len;
        check("extended: 0.22m past P2 with only 0.1m tolerance rejects",
              segments_intersect_extended(-1.0, y_past, 1.0, y_past,
                                          0.0, 0.0, 0.0, line_len,
                                          frac_tight),
              false);
    }

    // --- project_to_line: basic midpoint ---
    //
    // A unit horizontal line from (0,0) to (1,0). A point at (0.5, 0.25)
    // should project to u = 0.5 (midpoint), signed_d = 0.25 (above the
    // line). cross(CD=(1,0), CP=(0.5, 0.25)) = 1*0.25 - 0*0.5 = 0.25.
    {
        double u = -999.0, signed_d = -999.0;
        line_geometry::project_to_line(0.5, 0.25,
                                       0.0, 0.0, 1.0, 0.0,
                                       &u, &signed_d);
        check("project: midpoint u",
              std::fabs(u - 0.5) < 1e-12, true);
        check("project: midpoint signed_d",
              std::fabs(signed_d - 0.25) < 1e-12, true);
    }

    // --- project_to_line: endpoints ---
    // P at C gives u = 0; P at D gives u = 1; both signed_d = 0.
    {
        double u = 0.0, signed_d = 0.0;
        line_geometry::project_to_line(0.0, 0.0,
                                       0.0, 0.0, 1.0, 0.0,
                                       &u, &signed_d);
        check("project: P at C gives u=0",
              std::fabs(u) < 1e-12 && std::fabs(signed_d) < 1e-12, true);

        line_geometry::project_to_line(1.0, 0.0,
                                       0.0, 0.0, 1.0, 0.0,
                                       &u, &signed_d);
        check("project: P at D gives u=1",
              std::fabs(u - 1.0) < 1e-12 && std::fabs(signed_d) < 1e-12,
              true);
    }

    // --- project_to_line: extension past D ---
    // 2m past D on a 10m line ⇒ u = 1.2.  Firmware expects the overshoot
    // (u-1)*line_len to recover the physical 2m endpoint overshoot.
    {
        double u = 0.0, signed_d = 0.0;
        line_geometry::project_to_line(12.0, 0.0,
                                       0.0, 0.0, 10.0, 0.0,
                                       &u, &signed_d);
        check("project: 2m past D on 10m line → u=1.2",
              std::fabs(u - 1.2) < 1e-12, true);
        check("project: u=1.2 overshoot recovers 2m",
              std::fabs((u - 1.0) * 10.0 - 2.0) < 1e-12, true);
    }

    // --- project_to_line: extension past C ---
    // 3m past C on a 10m line ⇒ u = -0.3.
    {
        double u = 0.0, signed_d = 0.0;
        line_geometry::project_to_line(-3.0, 0.0,
                                       0.0, 0.0, 10.0, 0.0,
                                       &u, &signed_d);
        check("project: 3m past C → u=-0.3",
              std::fabs(u - (-0.3)) < 1e-12, true);
        check("project: u=-0.3 overshoot recovers 3m",
              std::fabs((-u) * 10.0 - 3.0) < 1e-12, true);
    }

    // --- project_to_line: sign convention ---
    // A vertical line C=(0,0) to D=(0,1).  A point at (1, 0.5) is to the
    // right when facing C→D.  cross(CD=(0,1), CP=(1,0.5)) = 0*0.5 - 1*1
    // = -1.  So signed_d = -1 for the right side (convention check).
    {
        double u = 0.0, signed_d = 0.0;
        line_geometry::project_to_line(1.0, 0.5,
                                       0.0, 0.0, 0.0, 1.0,
                                       &u, &signed_d);
        check("project: right of C→D gives negative signed_d",
              signed_d < 0.0, true);
        check("project: vertical line midpoint u=0.5",
              std::fabs(u - 0.5) < 1e-12, true);
    }

    // --- project_to_line: degenerate zero-length line ---
    {
        double u = 42.0, signed_d = 42.0;
        line_geometry::project_to_line(5.0, 5.0,
                                       1.0, 1.0, 1.0, 1.0,
                                       &u, &signed_d);
        check("project: degenerate line → u=0, signed_d=0",
              u == 0.0 && signed_d == 0.0, true);
    }

    // --- project_to_line: 2026-04-18 0.22m past P2 scenario ---
    // 10m vertical line from (0,0) to (0,10).  Walker crosses the
    // line's extension at y = 10.22.  Expect u = 1.022 and overshoot
    // = 0.22 m.
    {
        double u = 0.0, signed_d = 0.0;
        line_geometry::project_to_line(0.0, 10.22,
                                       0.0, 0.0, 0.0, 10.0,
                                       &u, &signed_d);
        check("project: 2026-04-18 scenario — u ≈ 1.022",
              std::fabs(u - 1.022) < 1e-9, true);
        check("project: 2026-04-18 scenario — overshoot ≈ 0.22m",
              std::fabs((u - 1.0) * 10.0 - 0.22) < 1e-9, true);
    }

    // --- 2026-04-19 codex regression: strict-boundary convention. ---
    //
    // segments_intersect() returns false when the crossing point
    // coincides with an endpoint of either segment (c1*c2 >= 0 path).
    // emit_candidate_event() in lap_timer_crossing.cpp therefore MUST
    // classify its geometric gate with STRICT inequalities
    // (`u > 0.0 && u < 1.0`) or the `result=PASS/REJECT` field of the
    // `[xing] candidate` log line will disagree with what
    // has_crossed_line() actually decides.  These regression cases
    // pin down the boundary semantics so the convention cannot drift.
    {
        // Line from (0,0) to (0,10). Path endpoint exactly on the line.
        check("boundary: path B on line segment (u=0.5) — strict reject",
              segments_intersect(-1.0, 5.0, 0.0, 5.0,
                                 0.0, 0.0, 0.0, 10.0),
              false);
        // Path through line's P1 exactly.
        check("boundary: path B at detection segment P1 (u=0)",
              segments_intersect(-1.0, 0.0, 0.0, 0.0,
                                 0.0, 0.0, 0.0, 10.0),
              false);
        // Path through line's P2 exactly.
        check("boundary: path B at detection segment P2 (u=1)",
              segments_intersect(-1.0, 10.0, 0.0, 10.0,
                                 0.0, 0.0, 0.0, 10.0),
              false);
        // Path fully straddling with u=0.5+epsilon should intersect.
        check("just-inside: u=0.5 crossing — accept",
              segments_intersect(-1.0, 5.0, 1.0, 5.0,
                                 0.0, 0.0, 0.0, 10.0),
              true);
    }

    if (fail_count == 0) {
        printf("test_line_geometry: OK\n");
        return 0;
    }
    return fail_count;
}
