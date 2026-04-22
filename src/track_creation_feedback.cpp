#include "track_creation_feedback.h"

#include <math.h>

bool track_creation_save_result_succeeded(TrackSaveResult result) {
    return result == TRACK_SAVE_RESULT_SUCCESS ||
           result == TRACK_SAVE_RESULT_SUCCESS_DIR_RECREATED;
}

const char* track_creation_save_result_message(TrackSaveResult result) {
    switch (result) {
        case TRACK_SAVE_RESULT_SUCCESS:
        case TRACK_SAVE_RESULT_SUCCESS_DIR_RECREATED:
            return "Track saved.";
        case TRACK_SAVE_RESULT_INVALID_ARGUMENT:
            return "Track save failed: invalid track data.";
        case TRACK_SAVE_RESULT_CAPACITY_REACHED:
            return "Track save failed: track storage is full.";
        case TRACK_SAVE_RESULT_FORMAT_FAILED:
            return "Track save failed: could not serialize the track.";
        case TRACK_SAVE_RESULT_DIRECTORY_CREATE_FAILED:
            return "Track save failed: could not create the tracks directory.";
        case TRACK_SAVE_RESULT_FILE_OPEN_FAILED:
            return "Track save failed: could not open the track file.";
        case TRACK_SAVE_RESULT_FILE_WRITE_SHORT:
            return "Track save failed: the SD write was incomplete.";
        case TRACK_SAVE_RESULT_FILE_SYNC_FAILED:
            return "Track save failed: the SD sync did not complete.";
        case TRACK_SAVE_RESULT_STORE_BUSY:
            return "Track save failed: track store busy, retry in a moment.";
        default:
            return "Track save failed.";
    }
}

double track_creation_start_finish_separation_m(const DetectionLine* line) {
    if (!line) {
        return 0.0;
    }

    const double kPi = 3.14159265358979323846;
    const double lat_mid_rad = ((line->lat1_deg + line->lat2_deg) * 0.5) * kPi / 180.0;
    const double dx = (line->lon2_deg - line->lon1_deg) * 111320.0 * cos(lat_mid_rad);
    const double dy = (line->lat2_deg - line->lat1_deg) * 110540.0;
    return sqrt(dx * dx + dy * dy);
}

bool track_creation_has_min_start_finish_separation(const DetectionLine* line, double min_distance_m) {
    return track_creation_start_finish_separation_m(line) >= min_distance_m;
}

double track_creation_min_save_line_length_m() {
#ifdef WALKING_TEST_MODE
    // Keep the walking-test profile tolerant so a small debug loop
    // around a 2-3 m line can still be saved.  The axial tolerance
    // (CROSSING_END_TOLERANCE_M = 2 m) already compensates for most
    // point-marking noise at this scale.
    return 2.0;
#else
    // Production: 5 m minimum.  Consumer GPS absolute error is 1-3 m
    // at both P1/P2 capture time and crossing time — shorter lines
    // make those errors comparable to the line itself.
    return 5.0;
#endif
}
