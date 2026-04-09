#include "track_runtime.h"

#include <string.h>

bool track_runtime_sync_detected_track(TrackDefinition* active_track,
                                       const TrackDefinition* detected,
                                       char* session_track_name,
                                       size_t session_track_name_len) {
    if (!active_track || !detected || !session_track_name
        || session_track_name_len == 0) {
        return false;
    }

    *active_track = *detected;
    strncpy(session_track_name, detected->name, session_track_name_len - 1);
    session_track_name[session_track_name_len - 1] = '\0';
    return true;
}

bool track_runtime_should_show_track_found(bool boot_track_confirmed,
                                           const TrackDefinition* active_track) {
    return boot_track_confirmed
        && active_track != nullptr
        && active_track->name[0] != '\0';
}

TrackDeleteDecision track_runtime_evaluate_delete(bool is_recording,
                                                  const TrackDefinition* active_track,
                                                  const char* delete_id) {
    if (!is_recording || !active_track || !delete_id) {
        return TRACK_DELETE_ALLOW;
    }

    if (active_track->id[0] != '\0'
        && strcmp(active_track->id, delete_id) == 0) {
        return TRACK_DELETE_BLOCK_ACTIVE_RECORDING;
    }

    return TRACK_DELETE_ALLOW;
}

bool track_runtime_should_commit_delete(bool storage_delete_ok) {
    return storage_delete_ok;
}

bool track_runtime_should_apply_late_auto_detect(bool is_recording,
                                                 const char* session_track_name) {
    if (is_recording) {
        return false;
    }

    if (session_track_name == nullptr || session_track_name[0] == '\0') {
        return true;
    }

    return strcmp(session_track_name, "No Track") == 0;
}
