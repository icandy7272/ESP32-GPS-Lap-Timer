#pragma once

#include <stddef.h>

#include "types.h"

typedef enum {
    TRACK_DELETE_ALLOW = 0,
    TRACK_DELETE_BLOCK_ACTIVE_RECORDING = 1,
} TrackDeleteDecision;

bool track_runtime_sync_detected_track(TrackDefinition* active_track,
                                       const TrackDefinition* detected,
                                       char* session_track_name,
                                       size_t session_track_name_len);

bool track_runtime_should_show_track_found(bool boot_track_confirmed,
                                           const TrackDefinition* active_track);

TrackDeleteDecision track_runtime_evaluate_delete(bool is_recording,
                                                  const TrackDefinition* active_track,
                                                  const char* delete_id);

bool track_runtime_should_commit_delete(bool storage_delete_ok);

bool track_runtime_should_apply_late_auto_detect(bool is_recording,
                                                 const char* session_track_name);
