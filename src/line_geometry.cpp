#include "line_geometry.h"

#include <math.h>

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

bool segments_intersect_extended(double ax, double ay,
                                 double bx, double by,
                                 double cx, double cy,
                                 double dx, double dy,
                                 double extension_fraction) {
    if (extension_fraction <= 0.0) {
        return segments_intersect(ax, ay, bx, by, cx, cy, dx, dy);
    }
    // Extend C-D axially by (extension_fraction * (D - C)) on each end.
    // Works in whatever coordinate system the caller uses; no sqrt / no
    // lat-to-meter conversion needed because the fraction is unitless.
    const double cdx = dx - cx;
    const double cdy = dy - cy;
    const double cx_ext = cx - cdx * extension_fraction;
    const double cy_ext = cy - cdy * extension_fraction;
    const double dx_ext = dx + cdx * extension_fraction;
    const double dy_ext = dy + cdy * extension_fraction;
    return segments_intersect(ax, ay, bx, by,
                              cx_ext, cy_ext,
                              dx_ext, dy_ext);
}

void project_to_line(double px, double py,
                     double cx, double cy,
                     double dx, double dy,
                     double* out_u,
                     double* out_signed_d) {
    const double cdx = dx - cx;
    const double cdy = dy - cy;
    const double cd_len_sq = cdx * cdx + cdy * cdy;
    if (cd_len_sq <= 0.0) {
        if (out_u)        *out_u = 0.0;
        if (out_signed_d) *out_signed_d = 0.0;
        return;
    }
    const double cpx = px - cx;
    const double cpy = py - cy;
    // u = dot(CP, CD) / |CD|^2 — parameter along the line (unitless).
    const double u = (cpx * cdx + cpy * cdy) / cd_len_sq;
    // signed_d = cross(CD, CP) / |CD| — perpendicular distance with
    // sign, in the same units as the inputs.
    const double cd_len = sqrt(cd_len_sq);
    const double signed_d = cross2d(cdx, cdy, cpx, cpy) / cd_len;
    if (out_u)        *out_u = u;
    if (out_signed_d) *out_signed_d = signed_d;
}

}  // namespace line_geometry
