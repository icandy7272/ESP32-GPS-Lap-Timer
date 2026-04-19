// ============================================================
// Draft Validation Line (Phase A + B + session fixes from
// 2026-04-19 codex follow-up review).
//
// Implements a parallel "dry-run" crossing evaluator for a line the
// operator is currently creating.  The lap_timer task runs the same
// segment-extended / heading-diff geometry against this line as it
// does against the active track's start/finish, but:
//
//   - NO arm / debounce state on the ACTIVE-track side is advanced
//   - NO lap / sector events are emitted
//   - NO session_state mutation
//
// Outputs:
//   - Serial `[xing-draft] candidate ... result=... reason=...`
//     lines for the laptop-side live_map tool.
//   - An in-memory ring buffer (last 16) + accepted/rejected counters
//     polled via /api/tracks/draft_validation.
//   - A monotonically-bumping session_id clients cache and compare
//     against poll responses to drop stale data (codex P1 from
//     2026-04-19 review: flip-direction + in-flight poll would
//     otherwise resurrect pre-flip counts).
//
// Session semantics
// -----------------
// Each call to lap_timer_set_draft_validation_line() bumps s_session_id
// and re-initialises state atomically.  process_draft_validation_line()
// samples the session_id along with the line snapshot, then re-checks
// it before incrementing counters so a concurrent set/clear cannot
// bleed an in-flight crossing into the new session.  The HTTP GET
// returns the session_id to the client, which compares against its
// cached value from POST and drops mismatched responses.
//
// Counter correctness
// -------------------
// The dry-run path now enforces the same MIN_CROSSING_SPEED_KMH and
// ARM_DISTANCE gates the real crossing path uses, so "stationary GPS
// drift" can no longer increment the accepted counter and satisfy the
// save gate.  See src/lap_timer/lap_timer_crossing.cpp for the
// reference implementation.
//
// Thread-safety
// -------------
// A portMUX spinlock guards all mutable state.  All public setters,
// getters, and the lap_timer-task worker hold the lock only for brief,
// bounded reads/writes — no geometry work or I/O happens inside the
// critical section.
// ============================================================

#include "lap_timer_internal.h"

#include "../line_geometry.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <math.h>
#include <string.h>

namespace {

constexpr int DRAFT_CANDIDATE_BUFFER = 16;

portMUX_TYPE s_draft_mux = portMUX_INITIALIZER_UNLOCKED;

// All fields guarded by s_draft_mux.
bool              s_draft_active     = false;
DetectionLine     s_draft_line       = {};
uint32_t          s_accepted_count   = 0;
uint32_t          s_rejected_count   = 0;
// Monotonic session counter.  Bumped once per set/clear.  Zero is a
// sentinel "no session ever" so clients can distinguish "firmware was
// rebooted" from "I missed a bump".  Wraps at UINT32_MAX ~= 136 years
// at 1 Hz — good enough.
uint32_t          s_session_id       = 0;
uint8_t           s_owner            = DRAFT_OWNER_NONE;
DraftCandidateEvent s_events[DRAFT_CANDIDATE_BUFFER] = {};
int               s_event_count      = 0;
int               s_event_next       = 0;
// Arm distance accumulator — matches lap_timer_crossing.cpp's
// ARM_DISTANCE_M gate so the draft counter cannot be satisfied by
// a single spurious side-flip right next to the line.
double            s_draft_arm_distance_m   = 0.0;
bool              s_draft_arm_ready        = false;

void reset_counters_and_buffer_locked() {
    s_accepted_count = 0;
    s_rejected_count = 0;
    s_event_count    = 0;
    s_event_next     = 0;
    s_draft_arm_distance_m = 0.0;
    s_draft_arm_ready      = false;
    memset(s_events, 0, sizeof(s_events));
}

void push_event_locked(const DraftCandidateEvent& ev) {
    s_events[s_event_next] = ev;
    s_event_next = (s_event_next + 1) % DRAFT_CANDIDATE_BUFFER;
    if (s_event_count < DRAFT_CANDIDATE_BUFFER) {
        s_event_count++;
    }
}

const char* reason_string(DraftCandidateReason r) {
    switch (r) {
        case DRAFT_REASON_SEGMENT:                    return "segment";
        case DRAFT_REASON_EXTENSION:                  return "extension";
        case DRAFT_REASON_HEADING_MISMATCH:           return "heading_mismatch";
        case DRAFT_REASON_OUTSIDE_ENDPOINT_TOLERANCE: return "outside_endpoint_tolerance";
        case DRAFT_REASON_NONE:                       return "";
    }
    return "";
}

const char* owner_name(uint8_t owner) {
    switch (owner) {
        case DRAFT_OWNER_SERIAL: return "SERIAL";
        case DRAFT_OWNER_WEB:    return "WEB";
        default:                 return "NONE";
    }
}

bool lines_match(const DetectionLine& a, const DetectionLine& b,
                 double tol_m, double tol_deg) {
    double d1 = ::haversine_m(a.lat1_deg, a.lon1_deg, b.lat1_deg, b.lon1_deg);
    double d2 = ::haversine_m(a.lat2_deg, a.lon2_deg, b.lat2_deg, b.lon2_deg);
    // Accept either endpoint-ordering — the operator may have marked
    // P1/P2 in reverse.  The heading comparison below disambiguates
    // direction: a swapped-endpoint line has the OPPOSITE heading in
    // firmware convention, so we reject it the same way as a true
    // 180° mismatch (see below).
    if ((d1 > tol_m || d2 > tol_m) &&
        (::haversine_m(a.lat1_deg, a.lon1_deg, b.lat2_deg, b.lon2_deg) > tol_m ||
         ::haversine_m(a.lat2_deg, a.lon2_deg, b.lat1_deg, b.lon1_deg) > tol_m)) {
        return false;
    }
    // Heading MUST match within tol_deg.  The previous branch that
    // also accepted a 180° delta was a save-gate bypass: a client
    // could validate direction X, then POST the same line with heading
    // X+180 and have passes_gate return true, reusing the accepted
    // count from the opposite direction without ever walking the
    // flipped direction.  Codex P1 from 2026-04-19 round-3 review.
    //
    // Flip Direction in the web UI already does the right thing by
    // re-POSTing (which installs a NEW line + new session_id + zero
    // counters), so this function does NOT need to be lenient about
    // 180° matches.  Operator walks the flipped direction, new
    // accepted count accrues, save gate passes with exact heading
    // match.
    float dh = fabsf(::heading_diff(a.valid_heading_deg, b.valid_heading_deg));
    if (dh > 180.0f) dh = 360.0f - dh;
    if (dh > tol_deg) {
        return false;
    }
    return true;
}

}  // namespace

// --- Public API (declared in lap_timer.h) -----------------------

uint32_t lap_timer_set_draft_validation_line(const DetectionLine* line,
                                             DraftValidationOwner owner) {
    uint32_t new_id = 0;
    uint8_t  prev_owner;
    bool     was_active;
    portENTER_CRITICAL(&s_draft_mux);
    prev_owner = s_owner;
    was_active = s_draft_active;
    // Overflow-free increment.  Skip 0 on wrap since we reserve it as
    // "no session" sentinel.
    s_session_id++;
    if (s_session_id == 0) s_session_id = 1;
    new_id = s_session_id;
    if (line == nullptr) {
        s_draft_active = false;
        s_owner        = DRAFT_OWNER_NONE;
        memset(&s_draft_line, 0, sizeof(s_draft_line));
    } else {
        s_draft_line   = *line;
        s_draft_active = true;
        s_owner        = static_cast<uint8_t>(owner);
    }
    reset_counters_and_buffer_locked();
    portEXIT_CRITICAL(&s_draft_mux);

    // Log cross-surface takeovers so the operator sees the serial
    // cancel / web POST collision instead of having state silently
    // replaced under them.
    if (was_active && line != nullptr && prev_owner != 0 &&
        prev_owner != static_cast<uint8_t>(owner)) {
        Serial.printf("[xing-draft] owner %s -> %s (session %u)\n",
                      owner_name(prev_owner),
                      owner_name(static_cast<uint8_t>(owner)),
                      (unsigned)new_id);
    }
    return new_id;
}

void lap_timer_get_draft_validation_snapshot(DraftValidationSnapshot* snapshot,
                                             DraftCandidateEvent* out_events,
                                             int max_events) {
    if (snapshot == nullptr) return;
    portENTER_CRITICAL(&s_draft_mux);
    snapshot->active     = s_draft_active;
    snapshot->session_id = s_session_id;
    snapshot->accepted   = s_accepted_count;
    snapshot->rejected   = s_rejected_count;
    snapshot->owner      = s_owner;

    int n = 0;
    if (out_events != nullptr && max_events > 0) {
        n = s_event_count < max_events ? s_event_count : max_events;
        for (int i = 0; i < n; i++) {
            int idx = (s_event_next - 1 - i + DRAFT_CANDIDATE_BUFFER)
                      % DRAFT_CANDIDATE_BUFFER;
            out_events[i] = s_events[idx];
        }
    }
    snapshot->event_count = n;
    portEXIT_CRITICAL(&s_draft_mux);
}

bool lap_timer_draft_validation_passes_gate(const DetectionLine* line,
                                            double  tol_m,
                                            double  tol_deg,
                                            uint32_t min_accepted) {
    if (line == nullptr) return false;
    // Evaluate the full gate inside ONE critical section — snapshot +
    // comparison + decision — so a concurrent set/clear cannot flip
    // the state between our read and the save's persistence.
    // lines_match is pure (haversine + heading_diff arithmetic, no
    // I/O, no allocations), so holding the spinlock across it is
    // cheap.  Codex P1 from 2026-04-19 round-3 review (TOCTOU).
    portENTER_CRITICAL(&s_draft_mux);
    bool passes = false;
    if (s_draft_active &&
        s_accepted_count >= min_accepted &&
        lines_match(s_draft_line, *line, tol_m, tol_deg)) {
        passes = true;
    }
    portEXIT_CRITICAL(&s_draft_mux);
    return passes;
}

uint32_t lap_timer_draft_validation_min_accepted() {
#ifdef WALKING_TEST_MODE
    return 1;
#else
    return 2;
#endif
}

// --- Deprecated aliases ------------------------------------------
// Kept for callers that haven't migrated to the atomic snapshot.
// Each still does one critical section so they never interleave
// within themselves, but multiple back-to-back calls can straddle
// a set/clear — new code should use the snapshot.

void lap_timer_get_draft_validation_counts(uint32_t* out_accepted,
                                           uint32_t* out_rejected) {
    portENTER_CRITICAL(&s_draft_mux);
    if (out_accepted) *out_accepted = s_accepted_count;
    if (out_rejected) *out_rejected = s_rejected_count;
    portEXIT_CRITICAL(&s_draft_mux);
}

int lap_timer_get_draft_validation_candidates(DraftCandidateEvent* out,
                                              int max_out) {
    if (out == nullptr || max_out <= 0) return 0;
    portENTER_CRITICAL(&s_draft_mux);
    int n = s_event_count < max_out ? s_event_count : max_out;
    for (int i = 0; i < n; i++) {
        int idx = (s_event_next - 1 - i + DRAFT_CANDIDATE_BUFFER)
                  % DRAFT_CANDIDATE_BUFFER;
        out[i] = s_events[idx];
    }
    portEXIT_CRITICAL(&s_draft_mux);
    return n;
}

bool lap_timer_draft_validation_active() {
    portENTER_CRITICAL(&s_draft_mux);
    bool active = s_draft_active;
    portEXIT_CRITICAL(&s_draft_mux);
    return active;
}

// --- Internal helper used by lap_timer_task ---------------------

namespace lap_timer_internal {

void process_draft_validation_line(const GpsPoint* prev,
                                   const GpsPoint* curr) {
    if (prev == nullptr || curr == nullptr) {
        return;
    }
    // Snapshot line + session_id under lock.  We'll re-check the id
    // before the counter/ring-buffer write so an intervening
    // set/clear can't bleed this candidate into a new session.
    DetectionLine line;
    uint32_t      snapshot_id;
    bool          active;
    portENTER_CRITICAL(&s_draft_mux);
    active      = s_draft_active;
    line        = s_draft_line;
    snapshot_id = s_session_id;
    portEXIT_CRITICAL(&s_draft_mux);
    if (!active) {
        return;
    }

    // --- Gate 1: MIN_CROSSING_SPEED_KMH ----------------------------
    // Real crossing path rejects curr->speed_kmh < MIN_CROSSING_SPEED_KMH
    // before even arming.  Mirror that here so stationary u-blox
    // drift cannot push the accepted counter past the save-gate
    // threshold.  Codex P1 from 2026-04-19 review.
    if (curr->speed_kmh < MIN_CROSSING_SPEED_KMH) {
        return;
    }

    // --- Gate 2: ARM_DISTANCE_M -----------------------------------
    // Private arm accumulator — the operator must have walked at
    // least ARM_DISTANCE_M since the line was installed (or since
    // the last accepted PASS) before a new crossing counts.  Without
    // this the first step after mark-p2 can produce a PASS that
    // satisfies walking-mode MIN_ACCEPTED_CROSSINGS=1.
    double step_m = ::haversine_m(prev->lat_deg, prev->lon_deg,
                                  curr->lat_deg, curr->lon_deg);
    portENTER_CRITICAL(&s_draft_mux);
    // Re-check session id: a set/clear may have happened between
    // the snapshot and now, resetting s_draft_arm_distance_m; in that
    // case our local step_m still applies to the NEW session's
    // distance budget (fair — the operator is still moving), but
    // our accepted/rejected contribution below will be dropped.
    if (s_session_id == snapshot_id) {
        s_draft_arm_distance_m += step_m;
        if (s_draft_arm_distance_m > ARM_DISTANCE_M) {
            s_draft_arm_ready = true;
        }
    }
    bool arm_ready = s_draft_arm_ready;
    portEXIT_CRITICAL(&s_draft_mux);
    if (!arm_ready) {
        return;
    }

    // Only act on side-flips.
    double lx = line.lat2_deg - line.lat1_deg;
    double ly = line.lon2_deg - line.lon1_deg;
    double s_prev = lx * (prev->lon_deg - line.lon1_deg)
                  - ly * (prev->lat_deg - line.lat1_deg);
    double s_curr = lx * (curr->lon_deg - line.lon1_deg)
                  - ly * (curr->lat_deg - line.lat1_deg);
    bool side_changed = (s_prev > 0.0) != (s_curr > 0.0);
    if (!side_changed) {
        return;
    }

    double line_len_m = ::haversine_m(line.lat1_deg, line.lon1_deg,
                                      line.lat2_deg, line.lon2_deg);
    double ext_fraction = 0.0;
    if (line_len_m > 0.1) {
        ext_fraction = CROSSING_END_TOLERANCE_M / line_len_m;
        if (ext_fraction > 1.0) ext_fraction = 1.0;
    }

    double denom = s_curr - s_prev;
    double t = 0.5;
    if (fabs(denom) > 1e-15) {
        t = -s_prev / denom;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
    }
    double xing_lat = prev->lat_deg + t * (curr->lat_deg - prev->lat_deg);
    double xing_lon = prev->lon_deg + t * (curr->lon_deg - prev->lon_deg);

    double u = 0.0;
    double signed_d_deg = 0.0;
    line_geometry::project_to_line(xing_lat, xing_lon,
                                   line.lat1_deg, line.lon1_deg,
                                   line.lat2_deg, line.lon2_deg,
                                   &u, &signed_d_deg);
    (void)signed_d_deg;

    double overshoot_m = 0.0;
    if (u < 0.0)      overshoot_m = -u * line_len_m;
    else if (u > 1.0) overshoot_m = (u - 1.0) * line_len_m;

    float hdiff = ::heading_diff(curr->heading_deg, line.valid_heading_deg);
    const bool heading_ok   = fabsf(hdiff) <= HEADING_WINDOW;
    const bool segment_ok   = (u > 0.0 && u < 1.0);
    const bool extension_ok = (u > -ext_fraction && u < 1.0 + ext_fraction);

    bool accepted;
    DraftCandidateReason reason;
    if (!heading_ok) {
        accepted = false;
        reason   = DRAFT_REASON_HEADING_MISMATCH;
    } else if (segment_ok) {
        accepted = true;
        reason   = DRAFT_REASON_SEGMENT;
    } else if (extension_ok) {
        accepted = true;
        reason   = DRAFT_REASON_EXTENSION;
    } else {
        accepted = false;
        reason   = DRAFT_REASON_OUTSIDE_ENDPOINT_TOLERANCE;
    }

    DraftCandidateEvent ev;
    ev.timestamp_us = curr->timestamp_us;
    ev.u            = u;
    ev.overshoot_m  = overshoot_m;
    ev.hdiff_deg    = hdiff;
    ev.accepted     = accepted;
    ev.reason       = static_cast<uint8_t>(reason);

    bool committed = false;
    portENTER_CRITICAL(&s_draft_mux);
    // Only commit if the session we sampled is still the live one —
    // otherwise an intervening set/clear means this candidate is
    // about a line that no longer exists.  Codex P1 from 2026-04-19.
    if (s_session_id == snapshot_id && s_draft_active) {
        if (accepted) {
            s_accepted_count++;
            // Drain the arm distance on a real PASS so successive
            // crossings each require a fresh walk-away.
            s_draft_arm_distance_m = 0.0;
            s_draft_arm_ready      = false;
        } else {
            s_rejected_count++;
        }
        push_event_locked(ev);
        committed = true;
    }
    portEXIT_CRITICAL(&s_draft_mux);

    // Serial log goes out whether or not the counter write happened —
    // it carries its own timestamp and the live_map side can tell by
    // the session_id in the /api response whether it was committed.
    // Not logging when committed=false would silently hide the race,
    // which is worse.
    Serial.printf(
        "[xing-draft] candidate u=%.3f overshoot=%.2f hdiff=%+.1f "
        "result=%s reason=%s%s\n",
        u, overshoot_m, (double)hdiff,
        accepted ? "PASS" : "REJECT",
        reason_string(reason),
        committed ? "" : " session_stale");
}

}  // namespace lap_timer_internal
