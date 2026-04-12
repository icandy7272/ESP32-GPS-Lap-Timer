#include "track_runtime.h"

#include <string.h>

namespace {

enum TrackRuntimeSource {
    TRACK_RUNTIME_SOURCE_NONE = 0,
    TRACK_RUNTIME_SOURCE_AUTO = 1,
    TRACK_RUNTIME_SOURCE_MANUAL = 2,
    TRACK_RUNTIME_SOURCE_NEWLY_CREATED = 3,
};

static volatile TrackRuntimeSource s_track_source = TRACK_RUNTIME_SOURCE_NONE;

bool has_valid_track_name(const char* session_track_name) {
    return session_track_name != nullptr
        && session_track_name[0] != '\0'
        && strcmp(session_track_name, "No Track") != 0;
}

const char* track_source_label(TrackRuntimeSource source) {
    switch (source) {
        case TRACK_RUNTIME_SOURCE_AUTO:
            return "Auto-detected";
        case TRACK_RUNTIME_SOURCE_MANUAL:
            return "Selected manually";
        case TRACK_RUNTIME_SOURCE_NEWLY_CREATED:
            return "Newly created";
        case TRACK_RUNTIME_SOURCE_NONE:
        default:
            return "Waiting to select";
    }
}

void copy_text(char* dest, size_t dest_len, const char* value) {
    if (!dest || dest_len == 0) {
        return;
    }

    if (!value) {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, value, dest_len - 1);
    dest[dest_len - 1] = '\0';
}

bool is_manual_source(TrackRuntimeSource source) {
    return source == TRACK_RUNTIME_SOURCE_MANUAL
        || source == TRACK_RUNTIME_SOURCE_NEWLY_CREATED;
}

}  // namespace

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
    track_runtime_note_auto_detect();
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

    if (is_manual_source(s_track_source)) {
        return false;
    }

    return !has_valid_track_name(session_track_name);
}

void track_runtime_note_auto_detect() {
    s_track_source = TRACK_RUNTIME_SOURCE_AUTO;
}

void track_runtime_note_manual_selection(bool newly_created) {
    s_track_source = newly_created
        ? TRACK_RUNTIME_SOURCE_NEWLY_CREATED
        : TRACK_RUNTIME_SOURCE_MANUAL;
}

void track_runtime_note_track_cleared() {
    s_track_source = TRACK_RUNTIME_SOURCE_NONE;
}

bool track_runtime_has_valid_selected_track(const TrackDefinition* active_track,
                                            const char* session_track_name) {
    if (!has_valid_track_name(session_track_name)) {
        return false;
    }

    if (active_track == nullptr) {
        return true;
    }

    return active_track->id[0] != '\0' || active_track->name[0] != '\0';
}

void track_runtime_fill_status(const TrackDefinition* active_track,
                               const char* session_track_name,
                               bool is_recording,
                               TrackRuntimeStatus* out_status) {
    if (!out_status) {
        return;
    }

    memset(out_status, 0, sizeof(*out_status));

    bool has_track =
        track_runtime_has_valid_selected_track(active_track, session_track_name);
    TrackRuntimeSource source = s_track_source;
    if (has_track && source == TRACK_RUNTIME_SOURCE_NONE) {
        source = TRACK_RUNTIME_SOURCE_AUTO;
    }

    if (has_track && active_track) {
        copy_text(out_status->track_id, sizeof(out_status->track_id),
                  active_track->id);
    }

    copy_text(out_status->track_source, sizeof(out_status->track_source),
              track_source_label(has_track ? source : TRACK_RUNTIME_SOURCE_NONE));
    out_status->track_locked_manual = has_track && is_manual_source(source);

    if (is_recording) {
        copy_text(out_status->recording_cta_state,
                  sizeof(out_status->recording_cta_state),
                  "recording");
        return;
    }

    if (!has_track) {
        copy_text(out_status->recording_cta_state,
                  sizeof(out_status->recording_cta_state),
                  "blocked_no_track");
        copy_text(out_status->recording_cta_reason,
                  sizeof(out_status->recording_cta_reason),
                  "Select a track before recording.");
        return;
    }

    copy_text(out_status->recording_cta_state,
              sizeof(out_status->recording_cta_state),
              "ready");
}
