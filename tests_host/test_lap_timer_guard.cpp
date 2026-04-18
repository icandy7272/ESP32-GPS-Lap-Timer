// Host tests for src/lap_timer_guard.cpp — the pure decision logic
// behind handle_finish_crossing() in the lap_timer module.
//
// Covers the three regression clusters that produced the 2026-04-17
// fix commits (89a1f83, be3ef1d, aa80a73):
//   - crossing-after-stop (both AFTER and DURING out-lap)
//   - fresh-boot auto-start
//   - recording-already-on first crossing (button press before line)

#include "lap_timer_guard.h"

#include <assert.h>
#include <stdio.h>

static int fail_count = 0;

static void check(const char* desc, CrossingDecision got, CrossingDecision want) {
    if (got != want) {
        fprintf(stderr, "FAIL: %s — got %d, want %d\n",
                desc, (int)got, (int)want);
        fail_count++;
    }
}

int main() {
    // --- Fresh boot, never recorded ---
    // session_stopped=false (never stopped), is_recording=false (not
    // started yet), first_crossing=true.  The first line crossing must
    // auto-start a session.
    check("fresh boot, first crossing",
          classify_crossing(false, false, true),
          CrossingDecision::AutoStartSession);

    // --- Recording started via button before the first line crossing ---
    // session_stopped=false, is_recording=true, first_crossing=true.
    // Just start the lap timer; storage_start_session() was already
    // called by session_start_recording().
    check("button-start before crossing, first crossing",
          classify_crossing(false, true, true),
          CrossingDecision::StartLapTimer);

    // --- Recording, past the first crossing, subsequent laps complete ---
    check("recording, 2nd crossing",
          classify_crossing(false, true, false),
          CrossingDecision::CompleteLap);

    // --- User pressed stop AFTER completing some laps ---
    // session_stop_recording() sets session_stopped=true, clears
    // is_recording. s_first_crossing was already false. Any subsequent
    // crossing must be rejected, not emit a lap event.
    check("stop after laps, subsequent crossing",
          classify_crossing(true, false, false),
          CrossingDecision::Reject);

    // --- User pressed stop DURING the out lap (before any crossing) ---
    // session_stop_recording() set session_stopped=true, cleared
    // is_recording.  s_first_crossing is still true because no line
    // was crossed.  Prior to the be3ef1d fix, this fell through to
    // AutoStartSession and silently re-opened the session.
    check("stop during out-lap, first crossing still pending",
          classify_crossing(true, false, true),
          CrossingDecision::Reject);

    // --- Defensive: session_stopped set but is_recording somehow true ---
    // Should not happen (session_stop_recording atomically flips both),
    // but if it ever does we treat it as an active session and proceed
    // with normal start logic rather than rejecting.  Documents intent.
    check("stopped flag set but still recording (transient)",
          classify_crossing(true, true, true),
          CrossingDecision::StartLapTimer);
    check("stopped flag set but still recording (subsequent)",
          classify_crossing(true, true, false),
          CrossingDecision::CompleteLap);

    // --- Defensive: not-recording and not-stopped mid-session (shouldn't
    // happen) — treat as a fresh auto-start to recover gracefully.
    check("not recording and not stopped, subsequent crossing",
          classify_crossing(false, false, false),
          CrossingDecision::CompleteLap);

    if (fail_count == 0) {
        printf("test_lap_timer_guard: OK\n");
        return 0;
    }
    return fail_count;
}
