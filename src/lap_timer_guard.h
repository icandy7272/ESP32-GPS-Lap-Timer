#pragma once

// Pure logic for deciding what to do on a start/finish line crossing.
// Extracted from handle_finish_crossing() in lap_timer_events.cpp so it
// can be host-tested without the FreeRTOS / Arduino surface.
//
// The state machine has three boolean inputs:
//   - session_stopped : session_state.session_stopped (set by
//                        session_stop_recording(), cleared by
//                        session_start_recording())
//   - is_recording    : session_state.is_recording
//   - first_crossing  : module-local s_first_crossing (true until the
//                        first crossing on this power-on / reset cycle)
//
// And resolves to one of four CrossingDecision values.  The real
// handler reads session_stopped + is_recording under s_session_mutex
// and s_first_crossing as a task-local bool, so the exact guarding is
// done in the handler; this module is the truth table only.

enum class CrossingDecision {
    // session was stopped by the user; reject every crossing until a
    // new session_start_recording() clears session_stopped.
    Reject,

    // fresh boot (s_first_crossing == true) AND not yet recording —
    // this is the auto-start path: set the lap-start timestamp, then
    // call storage_start_session() and mark session_state.is_recording.
    AutoStartSession,

    // first crossing but recording was already started externally
    // (button press before first line crossing): set the lap-start
    // timestamp, no storage_start_session() call.
    StartLapTimer,

    // second or later crossing on this power-on: compute lap time vs
    // s_lap_start_us, emit LAP_EVENT_FINISH, roll the timer forward.
    CompleteLap,
};

CrossingDecision classify_crossing(bool session_stopped,
                                   bool is_recording,
                                   bool first_crossing);
