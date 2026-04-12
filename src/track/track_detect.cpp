#include "track_internal.h"
#include "lap_timer.h"

double track_distance_to_center_m(const TrackDefinition* track,
                                  double lat, double lon) {
    if (!track) {
        return AUTO_DETECT_MAX_M + 1.0;
    }

    return haversine_m(lat, lon,
                       track->center_lat_deg,
                       track->center_lon_deg);
}

int track_find_nearby(double lat, double lon,
                      NearbyTrackCandidate* out, int max_results) {
    if (!out || max_results <= 0) {
        return 0;
    }

    int result_count = 0;
    for (int i = 0; i < s_track_count; i++) {
        double dist = track_distance_to_center_m(&s_tracks[i], lat, lon);
        if (dist >= AUTO_DETECT_MAX_M) {
            continue;
        }

        int insert_at = result_count;
        while (insert_at > 0 && out[insert_at - 1].distance_m > dist) {
            if (insert_at < max_results) {
                out[insert_at] = out[insert_at - 1];
            }
            insert_at--;
        }

        if (insert_at < max_results) {
            out[insert_at].track = &s_tracks[i];
            out[insert_at].distance_m = dist;
        }

        if (result_count < max_results) {
            result_count++;
        }
    }

    return result_count;
}

const TrackDefinition* track_auto_detect(double lat, double lon) {
    NearbyTrackCandidate nearby[1] = {};
    if (track_find_nearby(lat, lon, nearby, 1) > 0) {
        return nearby[0].track;
    }
    return nullptr;
}
