// ============================================================
// Draft Validation Line (Phase A of the 2026-04-18 finish-line
// live-map debugging roadmap).
//
// Implements a parallel "dry-run" crossing evaluator for a line the
// operator is currently creating.  The lap_timer task runs the same
// segment-extended / heading-diff geometry against this line as it
// does against the active track's start/finish, but:
//
//   - NO arm / debounce state is advanced
//   - NO lap / sector events are emitted
//   - NO session_state mutation
//
// The only outputs are:
//   - A serial log line per side-flip:
//       [xing-draft] candidate u=... overshoot=... hdiff=... \
//                    result=PASS|REJECT reason=<code>
//     for the laptop-side live_map tool (Phase A).
//   - An in-memory ring buffer of the last N candidates plus
//     accepted/rejected counters, queried by /api/tracks/draft_validation
//     for the phone-side web UI Validate step (Phase B).
//
// Thread-safety: a dedicated FreeRTOS mutex guards the line +
// counters + ring buffer.  Setters / readers (WiFi task, HTTP
// handlers, serial command handler) take it briefly.  The
// lap_timer task snapshots the current line into a stack variable
// under the mutex at the top of its iteration, then does the geometry
// work unlocked.
// ============================================================

#include "lap_timer_internal.h"

#include "../line_geometry.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <math.h>
#include <string.h>

namespace {

// Keep this modest — the web UI only wants to render the last ~8
// events to the operator, and a longer buffer wastes RAM on the
// ESP32-S3.  The ring buffer is write-always / read-snapshot.
constexpr int DRAFT_CANDIDATE_BUFFER = 16;

// Private mutex.  Created on first use so we don't depend on
// lap_timer_init() ordering.  Readers must take it briefly.
portMUX_TYPE s_draft_mux = portMUX_INITIALIZER_UNLOCKED;

// State.  All fields are guarded by s_draft_mux.
bool              s_draft_active = false;
DetectionLine     s_draft_line   = {};
uint32_t          s_accepted_count = 0;
uint32_t          s_rejected_count = 0;
DraftCandidateEvent s_events[DRAFT_CANDIDATE_BUFFER] = {};
int               s_event_count = 0;  // Logical size, clamped to buffer
int               s_event_next  = 0;  // Next write index (mod buffer)

void reset_counters_and_buffer_locked() {
    s_accepted_count = 0;
    s_rejected_count = 0;
    s_event_count    = 0;
    s_event_next     = 0;
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

}  // namespace

// --- Public API (declared in lap_timer.h) -----------------------

void lap_timer_set_draft_validation_line(const DetectionLine* line) {
    portENTER_CRITICAL(&s_draft_mux);
    if (line == nullptr) {
        s_draft_active = false;
        memset(&s_draft_line, 0, sizeof(s_draft_line));
    } else {
        s_draft_line   = *line;
        s_draft_active = true;
    }
    reset_counters_and_buffer_locked();
    portEXIT_CRITICAL(&s_draft_mux);
}

void lap_timer_get_draft_validation_counts(uint32_t* out_accepted,
                                           uint32_t* out_rejected) {
    portENTER_CRITICAL(&s_draft_mux);
    if (out_accepted) *out_accepted = s_accepted_count;
    if (out_rejected) *out_rejected = s_rejected_count;
    portEXIT_CRITICAL(&s_draft_mux);
}

int lap_timer_get_draft_validation_candidates(DraftCandidateEvent* out,
                                              int max_out) {
    if (out == nullptr || max_out <= 0) {
        return 0;
    }
    portENTER_CRITICAL(&s_draft_mux);
    int n = s_event_count < max_out ? s_event_count : max_out;
    // Copy newest-first.  s_event_next points at the slot that will
    // receive the NEXT write, so the newest event is at (next - 1).
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
    // Snapshot the line so we don't hold the mutex across the
    // geometry work (cheap, fixed-size copy).
    DetectionLine line;
    bool active;
    portENTER_CRITICAL(&s_draft_mux);
    active = s_draft_active;
    line   = s_draft_line;
    portEXIT_CRITICAL(&s_draft_mux);
    if (!active) {
        return;
    }

    // Only act on side-flips (same convention the real crossing path
    // uses — a candidate per topology change).  Computing signs here
    // duplicates the work process_line() does for the active track,
    // but the cost is a handful of FLOPS per GPS sample and keeps the
    // draft path isolated.
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

    // Same extension-fraction computation as the real gate.
    double line_len_m = haversine_m(line.lat1_deg, line.lon1_deg,
                                    line.lat2_deg, line.lon2_deg);
    double ext_fraction = 0.0;
    if (line_len_m > 0.1) {
        ext_fraction = CROSSING_END_TOLERANCE_M / line_len_m;
        if (ext_fraction > 1.0) ext_fraction = 1.0;
    }

    // Linear crossing point.
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

    float hdiff = heading_diff(curr->heading_deg, line.valid_heading_deg);
    const bool heading_ok   = fabsf(hdiff) <= HEADING_WINDOW;
    // STRICT inequalities match segments_intersect().  See the same
    // convention in emit_candidate_event() in lap_timer_crossing.cpp.
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

    portENTER_CRITICAL(&s_draft_mux);
    if (accepted) {
        s_accepted_count++;
    } else {
        s_rejected_count++;
    }
    push_event_locked(ev);
    portEXIT_CRITICAL(&s_draft_mux);

    // Serial log for the laptop-side live_map tool.  Format mirrors
    // the active-line `[xing] L<i> candidate ...` log so live_map
    // can share the parser with a tiny prefix change.
    Serial.printf(
        "[xing-draft] candidate u=%.3f overshoot=%.2f hdiff=%+.1f "
        "result=%s reason=%s\n",
        u, overshoot_m, (double)hdiff,
        accepted ? "PASS" : "REJECT",
        reason_string(reason));
}

}  // namespace lap_timer_internal
