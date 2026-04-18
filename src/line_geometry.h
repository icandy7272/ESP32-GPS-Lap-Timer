#pragma once

// Pure 2D segment intersection test.  Used by the lap timer's
// has_crossed_line() to decide whether a GPS path (prev -> curr) actually
// crosses the start/finish or sector DETECTION SEGMENT, rather than just
// crossing the infinite line that would extend through its two endpoints.
//
// The lap_timer module previously only tested for a sign-flip in the
// side_of_line() distance between prev and curr, which is mathematically
// equivalent to crossing the infinite-extended line.  In a small
// walking-test loop, any GPS position whose crossing point happened to
// fall far outside the actual [P1, P2] range triggered a false lap —
// most visibly as the 63.6s → 4.7s lap pair in the 2026-04-18 walk.
//
// This header is deliberately Arduino-free so tests_host can exercise
// the function without a full firmware build.

namespace line_geometry {

// Returns true iff the closed segments A-B and C-D intersect.
//
// Coordinates are taken as (latitude, longitude) degrees in our use,
// but the function is purely 2D — any consistent planar system works.
// GPS precision at the scales we care about (tens of metres) is well
// within the flat-earth approximation for degrees.
//
// Edge cases: if a segment endpoint lies exactly on the other segment,
// the result is false (the cross-product sign check on that side gets
// 0, which reads as "same side"); for GPS-driven paths this is
// astronomically improbable and not worth the extra code to handle.
bool segments_intersect(double ax, double ay,
                        double bx, double by,
                        double cx, double cy,
                        double dx, double dy);

}  // namespace line_geometry
