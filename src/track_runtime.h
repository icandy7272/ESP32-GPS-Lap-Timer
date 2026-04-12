#pragma once

#include <stddef.h>

#include "types.h"

typedef enum {
    TRACK_DELETE_ALLOW = 0,
    TRACK_DELETE_BLOCK_ACTIVE_RECORDING = 1,
} TrackDeleteDecision;

typedef struct {
    char track_id[32];
    char track_source[24];
    bool track_locked_manual;
    char recording_cta_state[24];
    char recording_cta_reason[96];
} TrackRuntimeStatus;

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

void track_runtime_note_auto_detect();

void track_runtime_note_manual_selection(bool newly_created);

void track_runtime_note_track_cleared();

bool track_runtime_has_valid_selected_track(const TrackDefinition* active_track,
                                            const char* session_track_name);

void track_runtime_fill_status(const TrackDefinition* active_track,
                               const char* session_track_name,
                               bool is_recording,
                               TrackRuntimeStatus* out_status);
