#pragma once

// Per-constellation satellite-count tracker, fed from NMEA $XXGSV
// sentences.  Surfaces a once-per-second `[gps-const] GPS=12 GAL=8
// BDS=10 GLO=7 QZS=0` line that tells the operator whether each
// constellation enabled in CFG-GNSS is actually producing usable
// satellites.  If Galileo is enabled but GAL=0 forever, the config
// was silently rejected despite apparent success.
//
// GSV talker IDs observed on u-blox M9N:
//   GP = GPS
//   GL = GLONASS
//   GA = Galileo
//   GB = BeiDou
//   GQ = QZSS
// SBAS satellites are carried inside GPGSV (PRN > 32), so we don't
// expose a separate SBAS count — the GPS field includes them.

#include <stdint.h>

struct ConstellationCounts {
    uint8_t gps;       // includes SBAS birds (PRN 120-158) tracked via GPGSV
    uint8_t glonass;
    uint8_t galileo;
    uint8_t beidou;
    uint8_t qzss;
};

// Note that a $XXGSV sentence was observed.  `talker` must point to
// a 2-char string matching one of GP / GL / GA / GB / GQ; other
// talkers are silently ignored.  `num_sats` is field 3 from the
// GSV sentence (total visible sats in that constellation).  Calling
// with num_sats < 0 is a no-op.
void gps_constellation_note_gsv(const char* talker, int num_sats);

// Snapshot the current per-constellation counts.  Typically polled
// from the 1 Hz diagnostic path in gps_fix.cpp.
ConstellationCounts gps_constellation_snapshot();

// Format the `[gps-const] GPS=N GAL=N BDS=N GLO=N QZS=N` line and
// print via Serial.  Called at 1 Hz alongside the existing [gps]
// summary in gps_send_fix_if_ready().  No-op if no GSV sentences
// have been observed yet (e.g., first second after boot before
// the receiver starts emitting any satellite data).
void gps_constellation_log_if_dirty();
