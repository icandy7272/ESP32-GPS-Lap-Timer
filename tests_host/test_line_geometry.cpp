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

    if (fail_count == 0) {
        printf("test_line_geometry: OK\n");
        return 0;
    }
    return fail_count;
}
