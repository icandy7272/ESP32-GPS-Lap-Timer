#include "track_runtime.h"

#include <assert.h>
#include <string.h>

static TrackDefinition make_track(const char* id, const char* name) {
    TrackDefinition track = {};
    strncpy(track.id, id, sizeof(track.id) - 1);
    strncpy(track.name, name, sizeof(track.name) - 1);
    return track;
}

static void test_sync_detected_track_updates_active_and_session_name() {
    TrackDefinition active_track = make_track("track_001", "Preloaded");
    TrackDefinition detected = make_track("track_002", "Detected");
    char session_track_name[64] = {0};

    bool changed = track_runtime_sync_detected_track(
        &active_track, &detected, session_track_name, sizeof(session_track_name));

    assert(changed);
    assert(strcmp(active_track.id, "track_002") == 0);
    assert(strcmp(active_track.name, "Detected") == 0);
    assert(strcmp(session_track_name, "Detected") == 0);
}

static void test_track_found_requires_confirmed_detection() {
    TrackDefinition active_track = make_track("track_001", "Preloaded");

    assert(!track_runtime_should_show_track_found(false, &active_track));
    assert(track_runtime_should_show_track_found(true, &active_track));
}

static void test_delete_policy_blocks_active_track_while_recording() {
    TrackDefinition active_track = make_track("track_001", "Current");

    TrackDeleteDecision decision = track_runtime_evaluate_delete(
        true, &active_track, "track_001");

    assert(decision == TRACK_DELETE_BLOCK_ACTIVE_RECORDING);
}

static void test_delete_policy_allows_non_active_or_not_recording() {
    TrackDefinition active_track = make_track("track_001", "Current");

    assert(track_runtime_evaluate_delete(true, &active_track, "track_002")
           == TRACK_DELETE_ALLOW);
    assert(track_runtime_evaluate_delete(false, &active_track, "track_001")
           == TRACK_DELETE_ALLOW);
}

static void test_delete_only_commits_on_storage_success() {
    assert(!track_runtime_should_commit_delete(false));
    assert(track_runtime_should_commit_delete(true));
}

static void test_late_auto_detect_only_applies_when_track_not_locked() {
    assert(track_runtime_should_apply_late_auto_detect(false, ""));
    assert(track_runtime_should_apply_late_auto_detect(false, "No Track"));
    assert(!track_runtime_should_apply_late_auto_detect(false, "Manual Track"));
    assert(!track_runtime_should_apply_late_auto_detect(true, ""));
}

int main() {
    test_sync_detected_track_updates_active_and_session_name();
    test_track_found_requires_confirmed_detection();
    test_delete_policy_blocks_active_track_while_recording();
    test_delete_policy_allows_non_active_or_not_recording();
    test_delete_only_commits_on_storage_success();
    test_late_auto_detect_only_applies_when_track_not_locked();
    return 0;
}
