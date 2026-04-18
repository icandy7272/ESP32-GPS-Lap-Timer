#include "lap_timer_guard.h"

CrossingDecision classify_crossing(bool session_stopped,
                                   bool is_recording,
                                   bool first_crossing) {
    // (1) User stop guard.  Set when session_stop_recording() runs and
    // cleared by session_start_recording(), so it cleanly rejects both
    // "stop AFTER first crossing" and "stop DURING out lap" without
    // needing s_first_crossing.
    if (session_stopped && !is_recording) {
        return CrossingDecision::Reject;
    }

    // (2) First crossing on this power-on / reset cycle.
    if (first_crossing) {
        if (!is_recording) {
            return CrossingDecision::AutoStartSession;
        }
        return CrossingDecision::StartLapTimer;
    }

    // (3) Subsequent crossings complete laps.
    return CrossingDecision::CompleteLap;
}
