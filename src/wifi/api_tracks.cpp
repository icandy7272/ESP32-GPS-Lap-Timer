// ============================================================
// /api/tracks handlers — list, create, select, delete
// ============================================================

#include "wifi_internal.h"
#include "../track.h"
#include "track_runtime.h"
#include "session.h"
#include "lap_timer.h"
#include "types.h"

#include <Arduino.h>
#include <WebServer.h>

// ============================================================
// GET /api/tracks — list track definitions (from in-memory array)
// ============================================================

void handle_api_tracks() {
    if (is_throttled()) {
        server.send(503, "application/json", "{\"error\":\"busy\"}");
        return;
    }

    // Iterate with the thread-safe copy-out APIs so a concurrent
    // track_save / track_delete on the main task (serial console
    // `track save` / `track select`) can't shift s_tracks[] out from
    // under us mid-iteration (codex review 2026-04-22 round 4 Low).
    String json = "{\"tracks\":[";
    int n = track_snapshot_count();
    int emitted = 0;
    for (int i = 0; i < n; i++) {
        TrackDefinition t = {};
        if (track_copy_at(i, &t) != TRACK_LOOKUP_OK) {
            break;  // store busy or mid-shift
        }
        if (emitted > 0) { json += ","; }
        json += "{\"id\":\"";
        json += jsonEscapeString(t.id);
        json += "\",\"name\":\"";
        json += jsonEscapeString(t.name);
        json += "\"}";
        emitted++;
    }
    json += "]}";
    server.send(200, "application/json", json);
}

// ============================================================
// POST /api/tracks — create new track definition
// ============================================================

/// Parse a float from JSON body by key name. Returns 0.0 if not found.
/// Prefer json_extract_double_checked() in new code — this variant
/// cannot distinguish "missing" from "present and zero".
static double json_extract_double(const char* json, const char* key) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return 0.0;
    p = strchr(p + strlen(needle), ':');
    if (!p) return 0.0;
    return strtod(p + 1, nullptr);
}

/// Returns true iff `key` exists in `json` and its value parses as a double.
static bool json_extract_double_checked(const char* json, const char* key,
                                        double* out) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return false;
    p = strchr(p + strlen(needle), ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    char* end = nullptr;
    double val = strtod(p, &end);
    if (end == p) return false;  // no digits parsed
    *out = val;
    return true;
}

static bool is_valid_lat(double deg) { return deg >= -90.0  && deg <= 90.0;  }
static bool is_valid_lon(double deg) { return deg >= -180.0 && deg <= 180.0; }
static bool is_valid_heading(double deg) { return deg >= 0.0 && deg < 360.0; }

// Returns true and leaves `line` populated iff all 5 required fields of a
// detection line are present in `json` and each coordinate falls within
// the valid range.  Heading must be in [0, 360).  The detection-line
// object in `json` is expected at the top level (no sector prefix); the
// caller is responsible for slicing the right block when parsing the
// sectors array.
//
// Passing nullptr for *_err_out suppresses the error message output.
static bool extract_detection_line(const char* json,
                                   const char* lat1_key,
                                   const char* lon1_key,
                                   const char* lat2_key,
                                   const char* lon2_key,
                                   const char* heading_key,
                                   DetectionLine* line,
                                   const char** err_out) {
    double lat1 = 0, lon1 = 0, lat2 = 0, lon2 = 0, heading = 0;
    if (!json_extract_double_checked(json, lat1_key, &lat1)) {
        if (err_out) *err_out = "missing coordinate";
        return false;
    }
    if (!json_extract_double_checked(json, lon1_key, &lon1)) {
        if (err_out) *err_out = "missing coordinate";
        return false;
    }
    if (!json_extract_double_checked(json, lat2_key, &lat2)) {
        if (err_out) *err_out = "missing coordinate";
        return false;
    }
    if (!json_extract_double_checked(json, lon2_key, &lon2)) {
        if (err_out) *err_out = "missing coordinate";
        return false;
    }
    if (!json_extract_double_checked(json, heading_key, &heading)) {
        if (err_out) *err_out = "missing heading";
        return false;
    }
    if (!is_valid_lat(lat1) || !is_valid_lat(lat2)) {
        if (err_out) *err_out = "latitude out of range";
        return false;
    }
    if (!is_valid_lon(lon1) || !is_valid_lon(lon2)) {
        if (err_out) *err_out = "longitude out of range";
        return false;
    }
    if (!is_valid_heading(heading)) {
        if (err_out) *err_out = "heading out of range (0-360)";
        return false;
    }
    line->lat1_deg = lat1;
    line->lon1_deg = lon1;
    line->lat2_deg = lat2;
    line->lon2_deg = lon2;
    line->valid_heading_deg = (float)heading;
    return true;
}

/// Extract a string from JSON by key. Writes to out, returns false if missing.
bool json_extract_str(const char* json, const char* key,
                      char* out, int out_len) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return false;
    p = strchr(p + strlen(needle), '"');
    if (!p) return false;
    p++;  // skip opening quote
    const char* end = strchr(p, '"');
    if (!end) return false;
    int len = end - p;
    if (len >= out_len) len = out_len - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

static bool is_ascii_safe(const char* s) {
    for (int i = 0; s[i]; i++) {
        char c = s[i];
        if (c < 0x20 || c > 0x7E) return false;
        if (c == '"' || c == '\\' || c == '/' || c == ':' ||
            c == '*' || c == '?' || c == '<' || c == '>' || c == '|') {
            return false;
        }
    }
    return true;
}

void handle_api_tracks_post() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }

    String body = server.arg("plain");
    if (body.length() < 10 || body.length() > 2048) {
        server.send(400, "application/json", "{\"error\":\"invalid body size\"}");
        return;
    }

    const char* json = body.c_str();

    // Build a TrackDefinition from the web form JSON
    TrackDefinition track = {};

    if (!json_extract_str(json, "name", track.name, sizeof(track.name))) {
        server.send(400, "application/json", "{\"error\":\"missing name\"}");
        return;
    }
    if (!is_ascii_safe(track.name)) {
        server.send(400, "application/json",
                    "{\"error\":\"name contains invalid characters\"}");
        return;
    }

    // Start/finish line — all five fields required with valid ranges.
    // Previously the handler accepted missing fields (json_extract_double
    // silently returned 0.0), so a half-formed request could write a
    // track whose geometry sat at (0,0) or had an invalid heading.
    const char* sf_err = nullptr;
    if (!extract_detection_line(json,
                                "sf_lat1", "sf_lon1",
                                "sf_lat2", "sf_lon2",
                                "sf_heading",
                                &track.start_finish,
                                &sf_err)) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "{\"error\":\"start/finish: %s\"}", sf_err);
        server.send(400, "application/json", buf);
        return;
    }
    const double min_len_m = track_creation_min_save_line_length_m();
    if (!track_creation_has_min_start_finish_separation(&track.start_finish, min_len_m)) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "{\"error\":\"start/finish points must be at least %.0f m apart\"}",
                 min_len_m);
        server.send(400, "application/json", buf);
        return;
    }

    // Firmware-side save gate.  The phone web UI already disables the
    // Create Track button until MIN_ACCEPTED_CROSSINGS walks have
    // landed, but a curl / devtools request can bypass the JS.  Enforce
    // the same gate here so the only way to persist a new start/finish
    // line is to have walked across it.  Codex P1 from 2026-04-19
    // follow-up review.
    const uint32_t min_acc = lap_timer_draft_validation_min_accepted();
    if (!lap_timer_draft_validation_passes_gate(
            &track.start_finish, /*tol_m=*/2.0, /*tol_deg=*/10.0,
            min_acc)) {
        char buf[192];
        snprintf(buf, sizeof(buf),
                 "{\"error\":\"not validated — walk across the line "
                 "at least %u time(s) before save\","
                 "\"code\":\"not_validated\","
                 "\"min_accepted\":%u}",
                 (unsigned)min_acc, (unsigned)min_acc);
        server.send(409, "application/json", buf);
        return;
    }

    // Compute center from start/finish midpoint
    track.center_lat_deg = (track.start_finish.lat1_deg + track.start_finish.lat2_deg) / 2.0;
    track.center_lon_deg = (track.start_finish.lon1_deg + track.start_finish.lon2_deg) / 2.0;

    // Parse optional sectors array
    int sector_lines = 0;
    const char* arr = strstr(json, "\"sectors\"");
    if (arr) {
        const char* pos = strchr(arr, '[');
        if (pos) {
            pos++;
            // Iterate up to 3 sector objects
            for (int si = 0; si < MAX_SECTORS - 1 && sector_lines < MAX_SECTORS - 1; si++) {
                const char* obj = strchr(pos, '{');
                if (!obj) break;
                const char* obj_end = strchr(obj, '}');
                if (!obj_end) break;

                // Extract into a temporary null-terminated block
                int blen = (int)(obj_end - obj + 1);
                if (blen > 0 && blen < 512) {
                    char blk[512];
                    memcpy(blk, obj, blen);
                    blk[blen] = '\0';

                    DetectionLine* sl = &track.sectors[sector_lines];
                    // Accept sector only when every required field is
                    // present and in range.  Silently skipping malformed
                    // sectors preserves the old "best-effort" behaviour
                    // while still refusing to persist an invalid
                    // geometry.
                    if (extract_detection_line(blk,
                                               "lat1", "lon1",
                                               "lat2", "lon2",
                                               "heading",
                                               sl, nullptr)) {
                        sector_lines++;
                    }
                }
                pos = obj_end + 1;
            }
        }
    }
    track.sector_count = sector_lines + 1;  // split lines + start/finish

    // Save through track module (validates, generates ID, writes proper JSON)
    // Use out_saved so we get the assigned id back directly; the
    // previous `track_get(track_count()-1)` peek was racy against a
    // concurrent save from the serial console path (codex review
    // 2026-04-22 round 4 Medium).
    TrackDefinition saved = {};
    TrackSaveResult save_result = track_save_detailed(&track, &saved);
    if (track_creation_save_result_succeeded(save_result)) {
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "{\"ok\":true,\"id\":\"%s\",\"name\":\"%s\"}",
                 saved.id, saved.name);
        server.send(201, "application/json", buf);
        return;
    } else {
        char buf[160];
        snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}",
                 track_creation_save_result_message(save_result));
        server.send(500, "application/json", buf);
    }
}

// ============================================================
// POST /api/tracks/select — set active track
// ============================================================

void handle_api_tracks_select() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }

    String body = server.arg("plain");
    const char* json = body.c_str();
    char id[32] = {};
    char source[24] = {};
    bool has_id = json_extract_str(json, "id", id, sizeof(id));
    bool has_source = json_extract_str(json, "source", source, sizeof(source));
    if (!has_id && !has_source) {
        server.send(400, "application/json", "{\"error\":\"missing id\"}");
        return;
    }
    bool use_auto = has_source && strcmp(source, "auto") == 0;

    // Delegate recording check + track lookup + track_name update
    // to session_try_claim_track_switch, the same helper the serial
    // console path uses.  Replaces the previous fail-open pattern
    // (pdMS_TO_TICKS(50) with recording defaulting to false on
    // contention) and the split-phase lap_timer_set_track() +
    // separate mutex take for track_name — codex review
    // 2026-04-22 HIGH.
    TrackDefinition snap = {};
    SessionTrackSwitchResult result = session_try_claim_track_switch(
        use_auto, has_id ? id : nullptr, &snap);

    switch (result) {
    case SESSION_TRACK_SWITCH_OK:
        break;
    case SESSION_TRACK_SWITCH_CONTENDED:
        server.send(503, "application/json",
                    "{\"error\":\"session busy — retry\"}");
        return;
    case SESSION_TRACK_SWITCH_RECORDING:
        server.send(409, "application/json",
                    "{\"error\":\"cannot switch track during recording\"}");
        return;
    case SESSION_TRACK_SWITCH_NO_FIX:
        server.send(409, "application/json",
                    "{\"error\":\"gps fix required for auto mode\"}");
        return;
    case SESSION_TRACK_SWITCH_NOT_FOUND:
        if (use_auto) {
            server.send(404, "application/json",
                        "{\"error\":\"no nearby track for auto mode\"}");
        } else {
            server.send(404, "application/json",
                        "{\"error\":\"track not found\"}");
        }
        return;
    }

    // Helper released session_mutex; safe to call lap_timer_set_track
    // now (it takes session_mutex internally).  snap is a caller-
    // owned copy, so a concurrent track_delete won't invalidate it.
    lap_timer_set_track(&snap);

    if (use_auto) {
        track_runtime_note_auto_detect();
    } else {
        track_runtime_note_manual_selection(
            has_source && strcmp(source, "newly_created") == 0);
    }

    // Emit the same [track] selected:/auto-detected: line the serial
    // path emits so live_map's active-track state stays in sync with
    // phone-app-initiated switches too.
    if (use_auto) {
        Serial.printf("[track] auto-detected: %s (%s)\n", snap.id, snap.name);
    } else {
        Serial.printf("[track] selected: %s (%s)\n", snap.id, snap.name);
    }

    String resp = "{\"ok\":true,\"name\":\"";
    resp += jsonEscapeString(snap.name);
    resp += "\"}";
    server.send(200, "application/json", resp);
}

// ============================================================
// POST /api/tracks/delete — delete a track
// ============================================================

void handle_api_tracks_delete() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }

    String body = server.arg("plain");
    const char* json = body.c_str();
    char id[32] = {};
    if (!json_extract_str(json, "id", id, sizeof(id))) {
        server.send(400, "application/json", "{\"error\":\"missing id\"}");
        return;
    }

    extern TrackDefinition active_track;

    // Snapshot the bits of SessionState and active_track we need to make
    // the is-this-the-active-track decision, all under session_mutex.
    // Reading active_track.id lock-free races with any concurrent writer
    // (e.g. a back-to-back select+delete), and lap_timer_task would
    // otherwise see the memset below tear.
    bool is_rec = false;
    char active_id_snapshot[sizeof(active_track.id)] = {0};
    if (xSemaphoreTake(session_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        is_rec = session_state.is_recording;
        memcpy(active_id_snapshot, active_track.id, sizeof(active_id_snapshot));
        xSemaphoreGive(session_mutex);
    }

    TrackDefinition tmp_active_for_decision = {};
    strlcpy(tmp_active_for_decision.id, active_id_snapshot,
            sizeof(tmp_active_for_decision.id));
    TrackDeleteDecision delete_decision =
        track_runtime_evaluate_delete(is_rec, &tmp_active_for_decision, id);

    if (delete_decision == TRACK_DELETE_BLOCK_ACTIVE_RECORDING) {
        server.send(409, "application/json",
                    "{\"error\":\"cannot delete active track during recording\"}");
        return;
    }

    // Existence check via copy-out so a concurrent save/delete
    // from the serial console can't race us (codex review
    // 2026-04-22 round 4 Low).  We don't actually need the copy
    // — track_delete below does its own lookup — but checking
    // first gives a precise 404 vs 500 distinction for the API.
    TrackDefinition existing_track = {};
    TrackLookupResult lr = track_copy_by_id(id, &existing_track);
    if (lr == TRACK_LOOKUP_BUSY) {
        server.send(503, "application/json",
                    "{\"error\":\"track store busy — retry\"}");
        return;
    }
    if (lr != TRACK_LOOKUP_OK) {
        server.send(404, "application/json", "{\"error\":\"track not found\"}");
        return;
    }

    bool was_active = (strcmp(active_id_snapshot, id) == 0);

    if (track_delete(id)) {
        if (was_active) {
            // Clear active_track under session_mutex and bump the version
            // so lap_timer_task refreshes its shadow before processing
            // the next GPS fix.  memset alone is a 120-byte non-atomic
            // write and was the canonical cross-task tear hazard.
            if (xSemaphoreTake(session_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                memset(&active_track, 0, sizeof(TrackDefinition));
                lap_timer_active_track_changed_locked();
                strlcpy(session_state.track_name, "No Track",
                        sizeof(session_state.track_name));
                xSemaphoreGive(session_mutex);
            }
            lap_timer_reset();
            track_runtime_note_track_cleared();
        }
        server.send(200, "application/json", "{\"ok\":true}");
    } else {
        server.send(500, "application/json", "{\"error\":\"delete failed\"}");
    }
}

// ============================================================
// /api/tracks/draft_validation — Phase B of the 2026-04-18 roadmap.
//
// Install a candidate detection line and let the phone UI poll
// PASS/REJECT counts + reasons as the operator walks across it.
// No persistence, no lap state, no session side-effects — this is
// purely a dry-run of the crossing geometry for save-time validation.
// ============================================================

static const char* draft_reason_to_string(uint8_t reason) {
    switch (reason) {
        case DRAFT_REASON_SEGMENT:                    return "segment";
        case DRAFT_REASON_EXTENSION:                  return "extension";
        case DRAFT_REASON_HEADING_MISMATCH:           return "heading_mismatch";
        case DRAFT_REASON_OUTSIDE_ENDPOINT_TOLERANCE: return "outside_endpoint_tolerance";
        case DRAFT_REASON_NONE:
        default:                                      return "";
    }
}

// POST /api/tracks/draft_validation
// Body: {"sf_lat1":..,"sf_lon1":..,"sf_lat2":..,"sf_lon2":..,"sf_heading":..}
//
// Accepts the same field names used by POST /api/tracks create so the
// JS can reuse its existing serializer.  Resets the counters + ring
// buffer every time a line is installed — a Flip Direction press
// re-POSTs the same P1/P2 with heading ± 180° and starts fresh.
void handle_api_tracks_draft_validation_post() {
    if (is_throttled()) {
        server.send(503, "application/json", "{\"error\":\"busy\"}");
        return;
    }
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }
    String body = server.arg("plain");
    const char* json = body.c_str();
    DetectionLine line = {};
    const char* err = nullptr;
    if (!extract_detection_line(json,
                                "sf_lat1", "sf_lon1",
                                "sf_lat2", "sf_lon2",
                                "sf_heading",
                                &line, &err)) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "{\"error\":\"draft_validation: %s\"}", err ? err : "invalid");
        server.send(400, "application/json", buf);
        return;
    }
    uint32_t session_id = lap_timer_set_draft_validation_line(
        &line, DRAFT_OWNER_WEB);
    // Return the new session_id so the client can cache it and drop
    // any GET response that doesn't match.  Closes the Flip Direction
    // / re-mark race codex flagged in the 2026-04-19 follow-up.
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"ok\":true,\"session_id\":%u}", (unsigned)session_id);
    server.send(200, "application/json", buf);
}

// GET /api/tracks/draft_validation
// Returns:
//   {"active":bool,"accepted":N,"rejected":N,
//    "events":[{"ts_us":..,"u":..,"overshoot_m":..,"hdiff_deg":..,
//               "accepted":bool,"reason":"..."}...]}
//
// `events` is newest-first, capped at the firmware ring buffer depth
// (currently 16).  Client should poll at ~500 ms.
void handle_api_tracks_draft_validation_get() {
    // One-shot atomic snapshot: active / session_id / counts / owner /
    // events all from the same critical section so the client cannot
    // get a mixed response like "active=false with non-empty events"
    // codex flagged in the 2026-04-19 follow-up review.
    constexpr int kMaxEvents = 16;
    DraftValidationSnapshot snap = {};
    DraftCandidateEvent     events[kMaxEvents];
    lap_timer_get_draft_validation_snapshot(&snap, events, kMaxEvents);

    const char* owner_str = "none";
    switch (snap.owner) {
        case DRAFT_OWNER_SERIAL: owner_str = "serial"; break;
        case DRAFT_OWNER_WEB:    owner_str = "web";    break;
        default:                 owner_str = "none";   break;
    }

    String json;
    json.reserve(256 + snap.event_count * 96);
    json += "{\"active\":";
    json += snap.active ? "true" : "false";
    json += ",\"session_id\":";
    json += (unsigned long)snap.session_id;
    json += ",\"owner\":\"";
    json += owner_str;
    json += "\",\"accepted\":";
    json += snap.accepted;
    json += ",\"rejected\":";
    json += snap.rejected;
    json += ",\"min_accepted\":";
    json += lap_timer_draft_validation_min_accepted();
    json += ",\"events\":[";
    for (int i = 0; i < snap.event_count; i++) {
        if (i > 0) json += ",";
        const DraftCandidateEvent& e = events[i];
        char buf[192];
        snprintf(buf, sizeof(buf),
                 "{\"ts_us\":%lld,\"u\":%.4f,\"overshoot_m\":%.3f,"
                 "\"hdiff_deg\":%.2f,\"accepted\":%s,\"reason\":\"%s\"}",
                 (long long)e.timestamp_us,
                 e.u, e.overshoot_m, (double)e.hdiff_deg,
                 e.accepted ? "true" : "false",
                 draft_reason_to_string(e.reason));
        json += buf;
    }
    json += "]}";
    server.send(200, "application/json", json);
}

// DELETE /api/tracks/draft_validation[?session=N]
// Clears the installed line, resets counters / ring buffer.  Returns
// the new session_id (assigned by the clear operation) so the client
// can cache it and recognise its own orphan-cleanup on the next GET.
//
// Optional `session` query parameter — if provided, the DELETE only
// clears when the CURRENT firmware session_id matches.  This scopes
// the cleanup to a specific session the client had cached, closing
// the GET-then-DELETE TOCTOU where a newer session installed between
// the GET response and the DELETE request could be silently killed.
// Codex P2 from 2026-04-20 round-4 review.
//
// Response shape when a session guard is supplied and does NOT match:
//   {"ok":false,"skipped":true,"session_id":<current>,"expected":<arg>}
// The client should treat this as success (state is what it wants
// anyway — we didn't touch it).
void handle_api_tracks_draft_validation_delete() {
    bool has_guard = server.hasArg("session");
    uint32_t guard_id = 0;
    if (has_guard) {
        guard_id = (uint32_t)strtoul(server.arg("session").c_str(), nullptr, 10);
    }

    if (has_guard) {
        // Atomic conditional clear — the firmware helper checks the
        // current session_id and only clears if it matches, all
        // inside one critical section.  If the guard didn't match,
        // returns 0 (and we return skipped=true to the client).
        uint32_t new_id = lap_timer_clear_draft_validation_if(guard_id);
        if (new_id == 0) {
            DraftValidationSnapshot snap = {};
            lap_timer_get_draft_validation_snapshot(&snap, nullptr, 0);
            char buf[96];
            snprintf(buf, sizeof(buf),
                     "{\"ok\":false,\"skipped\":true,"
                     "\"session_id\":%u,\"expected\":%u}",
                     (unsigned)snap.session_id, (unsigned)guard_id);
            server.send(200, "application/json", buf);
            return;
        }
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "{\"ok\":true,\"session_id\":%u}", (unsigned)new_id);
        server.send(200, "application/json", buf);
        return;
    }
    uint32_t new_id = lap_timer_set_draft_validation_line(
        nullptr, DRAFT_OWNER_NONE);
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"ok\":true,\"session_id\":%u}", (unsigned)new_id);
    server.send(200, "application/json", buf);
}
