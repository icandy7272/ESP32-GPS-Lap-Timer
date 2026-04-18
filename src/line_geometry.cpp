#include "line_geometry.h"

namespace line_geometry {

namespace {

inline double cross2d(double ax, double ay, double bx, double by) {
    return ax * by - ay * bx;
}

}  // namespace

bool segments_intersect(double ax, double ay,
                        double bx, double by,
                        double cx, double cy,
                        double dx, double dy) {
    // Segment AB vector, segment CD vector.
    const double abx = bx - ax;
    const double aby = by - ay;
    const double cdx = dx - cx;
    const double cdy = dy - cy;

    // Are C and D on strictly opposite sides of the line through AB?
    // Multiplication is negative iff the signs differ.  Zero (point on
    // the line) counts as "not strictly crossing", matching our
    // documented endpoint-on-segment behaviour.
    const double c1 = cross2d(abx, aby, cx - ax, cy - ay);
    const double c2 = cross2d(abx, aby, dx - ax, dy - ay);
    if (c1 * c2 >= 0.0) {
        return false;
    }

    // Are A and B on strictly opposite sides of the line through CD?
    const double c3 = cross2d(cdx, cdy, ax - cx, ay - cy);
    const double c4 = cross2d(cdx, cdy, bx - cx, by - cy);
    if (c3 * c4 >= 0.0) {
        return false;
    }

    return true;
}

}  // namespace line_geometry
