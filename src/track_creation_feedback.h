#pragma once

#include "types.h"

enum TrackSaveResult {
    TRACK_SAVE_RESULT_SUCCESS = 0,
    TRACK_SAVE_RESULT_SUCCESS_DIR_RECREATED = 1,
    TRACK_SAVE_RESULT_INVALID_ARGUMENT = 2,
    TRACK_SAVE_RESULT_CAPACITY_REACHED = 3,
    TRACK_SAVE_RESULT_FORMAT_FAILED = 4,
    TRACK_SAVE_RESULT_DIRECTORY_CREATE_FAILED = 5,
    TRACK_SAVE_RESULT_FILE_OPEN_FAILED = 6,
    TRACK_SAVE_RESULT_FILE_WRITE_SHORT = 7,
    TRACK_SAVE_RESULT_FILE_SYNC_FAILED = 8,
};

bool track_creation_save_result_succeeded(TrackSaveResult result);
const char* track_creation_save_result_message(TrackSaveResult result);

double track_creation_start_finish_separation_m(const DetectionLine* line);
bool track_creation_has_min_start_finish_separation(const DetectionLine* line, double min_distance_m);
