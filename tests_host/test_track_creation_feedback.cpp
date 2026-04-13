#include "track_creation_feedback.h"

#include <assert.h>
#include <string.h>

static DetectionLine make_line(double lat1, double lon1, double lat2, double lon2) {
    DetectionLine line = {};
    line.lat1_deg = lat1;
    line.lon1_deg = lon1;
    line.lat2_deg = lat2;
    line.lon2_deg = lon2;
    line.valid_heading_deg = 180.0f;
    return line;
}

static void test_save_result_messages_are_specific() {
    assert(strcmp(track_creation_save_result_message(TRACK_SAVE_RESULT_DIRECTORY_CREATE_FAILED),
                  "Track save failed: could not create the tracks directory.") == 0);
    assert(strcmp(track_creation_save_result_message(TRACK_SAVE_RESULT_FILE_OPEN_FAILED),
                  "Track save failed: could not open the track file.") == 0);
    assert(strcmp(track_creation_save_result_message(TRACK_SAVE_RESULT_FILE_WRITE_SHORT),
                  "Track save failed: the SD write was incomplete.") == 0);
    assert(strcmp(track_creation_save_result_message(TRACK_SAVE_RESULT_FILE_SYNC_FAILED),
                  "Track save failed: the SD sync did not complete.") == 0);
}

static void test_save_success_variants_are_treated_as_success() {
    assert(track_creation_save_result_succeeded(TRACK_SAVE_RESULT_SUCCESS));
    assert(track_creation_save_result_succeeded(TRACK_SAVE_RESULT_SUCCESS_DIR_RECREATED));
    assert(!track_creation_save_result_succeeded(TRACK_SAVE_RESULT_FILE_OPEN_FAILED));
}

static void test_start_finish_minimum_separation() {
    DetectionLine short_line = make_line(31.2300000, 121.4700000, 31.2300000, 121.4700050);
    DetectionLine valid_line = make_line(31.2300000, 121.4700000, 31.2300000, 121.4700200);

    assert(track_creation_start_finish_separation_m(&short_line) < 1.0);
    assert(!track_creation_has_min_start_finish_separation(&short_line, 1.0));

    assert(track_creation_start_finish_separation_m(&valid_line) > 1.0);
    assert(track_creation_has_min_start_finish_separation(&valid_line, 1.0));
}

int main() {
    test_save_result_messages_are_specific();
    test_save_success_variants_are_treated_as_success();
    test_start_finish_minimum_separation();
    return 0;
}
