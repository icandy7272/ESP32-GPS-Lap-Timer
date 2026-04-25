#include "crossing_time.h"

#include <math.h>

namespace {

static constexpr int BINARY_SEARCH_ITS = 12;

static double cross_product_2d_local(double ax, double ay,
                                     double bx, double by) {
    return ax * by - ay * bx;
}

static double side_of_line(double plat, double plon,
                           const DetectionLine* line) {
    double lx = line->lat2_deg - line->lat1_deg;
    double ly = line->lon2_deg - line->lon1_deg;
    double px = plat - line->lat1_deg;
    double py = plon - line->lon1_deg;
    return cross_product_2d_local(lx, ly, px, py);
}

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

static double spline_find_crossing(const GpsPoint* p0,
                                   const GpsPoint* p1,
                                   const GpsPoint* p2,
                                   const GpsPoint* p3,
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

static double linear_crossing_t(const GpsPoint* prev,
                                const GpsPoint* curr,
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

static int64_t interpolate_time_us(const GpsPoint* p1,
                                   const GpsPoint* p2,
                                   double t) {
    int64_t dt = p2->timestamp_us - p1->timestamp_us;
    return p1->timestamp_us + (int64_t)(t * (double)dt);
}

}  // namespace

int64_t crossing_time_linear_us(const GpsPoint* prev,
                                const GpsPoint* curr,
                                const DetectionLine* line) {
    if (prev == nullptr || curr == nullptr || line == nullptr) {
        return 0;
    }
    double t = linear_crossing_t(prev, curr, line);
    return interpolate_time_us(prev, curr, t);
}

int64_t crossing_time_centered_us(const GpsPoint* p0,
                                  const GpsPoint* p1,
                                  const GpsPoint* p2,
                                  const GpsPoint* p3,
                                  const DetectionLine* line) {
    if (p1 == nullptr || p2 == nullptr || line == nullptr) {
        return 0;
    }
    if (p0 == nullptr || p3 == nullptr ||
        p2->timestamp_us <= p1->timestamp_us) {
        return crossing_time_linear_us(p1, p2, line);
    }

    double t = spline_find_crossing(p0, p1, p2, p3, line);
    if (!isfinite(t) || t < 0.0 || t > 1.0) {
        return crossing_time_linear_us(p1, p2, line);
    }
    return interpolate_time_us(p1, p2, t);
}
