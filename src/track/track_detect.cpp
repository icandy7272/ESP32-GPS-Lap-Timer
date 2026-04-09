#include "track_internal.h"
#include "lap_timer.h"

const TrackDefinition* track_auto_detect(double lat, double lon) {
    const TrackDefinition* best = nullptr;
    double best_dist = AUTO_DETECT_MAX_M;

    for (int i = 0; i < s_track_count; i++) {
        double dist = haversine_m(lat, lon,
                                  s_tracks[i].center_lat_deg,
                                  s_tracks[i].center_lon_deg);
        if (dist < best_dist) {
            best_dist = dist;
            best = &s_tracks[i];
        }
    }
    return best;
}
