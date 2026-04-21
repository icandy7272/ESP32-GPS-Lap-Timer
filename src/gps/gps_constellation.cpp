#include "gps/gps_constellation.h"

#include <Arduino.h>
#include <string.h>

namespace {

ConstellationCounts s_counts = {};
bool s_dirty = false;

// Update `slot` only if the new value is >= the current one, and
// flag dirty.  GSV sentences can arrive split across multiple
// messages within the same cycle (up to 4 sats per sentence), so
// the num_sats field in the first sentence of a cycle is the one
// that carries the total for that constellation.  Rather than
// tracking cycle state here, we just keep the max observed value
// and reset once per log line — close enough for a diagnostic.
void set_max(uint8_t& slot, int num_sats) {
    if (num_sats < 0 || num_sats > 255) return;
    uint8_t v = static_cast<uint8_t>(num_sats);
    if (v != slot) {
        slot = (v > slot) ? v : slot;  // clamp-up within cycle
        s_dirty = true;
    }
}

}  // namespace

void gps_constellation_note_gsv(const char* talker, int num_sats) {
    if (talker == nullptr) return;
    if (talker[0] != 'G') return;
    switch (talker[1]) {
    case 'P': set_max(s_counts.gps,     num_sats); break;
    case 'L': set_max(s_counts.glonass, num_sats); break;
    case 'A': set_max(s_counts.galileo, num_sats); break;
    case 'B': set_max(s_counts.beidou,  num_sats); break;
    case 'Q': set_max(s_counts.qzss,    num_sats); break;
    default: break;
    }
}

ConstellationCounts gps_constellation_snapshot() {
    return s_counts;
}

void gps_constellation_log_if_dirty() {
    if (!s_dirty) {
        return;
    }
    // Emit the structured `[gps-const]` line for live_map ingestion.
    // Same cadence as the existing [gps] 1 Hz summary; a tool that
    // parses both can pin per-constellation trends over time.
    Serial.printf("[gps-const] GPS=%u GAL=%u BDS=%u GLO=%u QZS=%u\n",
                  s_counts.gps, s_counts.galileo, s_counts.beidou,
                  s_counts.glonass, s_counts.qzss);
    // Reset the max-within-cycle window so the next second's line
    // reflects that second's satellite availability, not a stale
    // one-off high count.
    s_counts = {};
    s_dirty = false;
}
