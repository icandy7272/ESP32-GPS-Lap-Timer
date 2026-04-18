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

// Like segments_intersect, but first extends the C-D segment axially
// outward on each end by (extension_fraction * |CD|).  Exists to
// absorb absolute GPS position error: when the user marks track points
// and when they later walk past them, both recorded coordinates carry
// 1-3 m of independent noise.  Strict segment intersection rejects the
// "I walked 0.22 m past the P2 endpoint" case even though the walker
// physically crossed the line — see the 2026-04-18 walk-test where this
// was the exact failure mode.  Axial extension absorbs that noise
// without re-opening the infinite-line bug the bare segment check was
// introduced to fix.
//
// extension_fraction <= 0 is equivalent to segments_intersect().
// Typical values: 0.1–0.3 (10–30 % on each end).  Callers that want a
// meter-scale tolerance should compute the fraction themselves from the
// line's real length (fraction = tolerance_m / length_m).
bool segments_intersect_extended(double ax, double ay,
                                 double bx, double by,
                                 double cx, double cy,
                                 double dx, double dy,
                                 double extension_fraction);

}  // namespace line_geometry
