#pragma once

// ============================================================
// UBX protocol frame builder — pure, hardware-independent.
//
// u-blox binary protocol frames have the layout:
//
//   0xB5 0x62 | cls | id | len_lo | len_hi | payload... | ck_a | ck_b
//
// Checksum is Fletcher-8 over (cls, id, len_lo, len_hi, payload).
//
// This module owns the byte-level layout so it can be unit-tested
// on a host compiler without Arduino / Serial2 / delay() — the I/O
// caller (src/gps/gps_ubx.cpp) just copies the buffer to the UART.
// ============================================================

#include <stddef.h>
#include <stdint.h>

struct UbxChecksum {
    uint8_t ck_a;
    uint8_t ck_b;
};

// Fletcher-8 checksum over a byte range — used internally by
// ubx_build_frame but exposed for tests and for callers that need
// to validate incoming frames.  Never modifies input.
UbxChecksum ubx_compute_checksum(const uint8_t* data, size_t len);

// Build a complete UBX frame (sync bytes + header + payload + checksum)
// into `out`.  Returns total bytes written, or -1 if `out_cap` is too
// small.  `payload` may be nullptr iff `payload_len == 0`.
int ubx_build_frame(uint8_t cls, uint8_t id,
                    const uint8_t* payload, uint16_t payload_len,
                    uint8_t* out, size_t out_cap);

// Build a UBX-CFG-NAV5 (0x06 0x24) frame that sets three fields
// tuned for kart/car track use:
//
//   dynModel          = 4   (Automotive motion model)
//   minElev           = 10° (reject low-elevation sats — these are
//                            mostly multipath off buildings/trees)
//   staticHoldThresh  = 5 cm/s (snap position when nearly
//                               stationary, eliminating "parked
//                               but drifting" wobble)
//
// Mask bits 0 (dynModel), 1 (minEl), and 6 (staticHoldMask) are
// set; every other CFG-NAV5 field keeps its factory default.
//
// Output size: 44 bytes (2 sync + 4 header + 36 payload + 2 checksum).
//
// Returns bytes written, or -1 if `out_cap < 44`.
int ubx_build_cfg_nav5_automotive(uint8_t* out, size_t out_cap);

// Build a UBX-CFG-SBAS (0x06 0x16) frame that enables SBAS for
// ranging + differential correction, tracking up to 3 SBAS birds
// with auto-scan (picks the regionally-available system — WAAS,
// EGNOS, BDSBAS, MSAS).
//
// Output size: 16 bytes (2 + 4 + 8 payload + 2 checksum).
//
// Returns bytes written, or -1 if `out_cap < 16`.
int ubx_build_cfg_sbas_enable(uint8_t* out, size_t out_cap);

// Build a UBX-CFG-GNSS (0x06 0x3E) frame enabling all major L1
// constellations on a u-blox M9N: GPS L1C/A, SBAS L1C/A, Galileo
// E1, BeiDou B1I, QZSS L1C/A, GLONASS L1OF.
//
// QZSS is deliberately L1C/A only (sigCfgMask 0x01, not 0x05): the
// L1S safety signal is Japan-region specific, and a previous encoding
// of 0x00010005 accidentally set bit 2 (an undefined sigCfgMask bit)
// which u-blox NAKs silently for the entire CFG-GNSS message — taking
// the whole multi-constellation config down with it.  Keep it L1C/A.
// See src/ubx_builder.cpp for the full decision log.
//
// Each of the 6 config blocks has its enable bit set and a
// sigCfgMask appropriate for M9N's L1-only hardware.
//
// Output size: 60 bytes (2 + 4 + (4+6*8) payload + 2 checksum).
//
// Returns bytes written, or -1 if `out_cap < 60`.
int ubx_build_cfg_gnss_all(uint8_t* out, size_t out_cap);

// Build a CFG-GNSS poll frame (same class/id, zero payload).  The
// receiver replies with a full CFG-GNSS frame carrying its current
// per-constellation configuration — the ground truth for verifying
// that an earlier CFG-GNSS SET actually took effect.
//
// Output size: 8 bytes (sync + header + no payload + checksum).
//
// Returns bytes written, or -1 if `out_cap < 8`.
int ubx_build_cfg_gnss_poll(uint8_t* out, size_t out_cap);

// Decoded enable bits from a CFG-GNSS response payload.  One bool
// per constellation the lap timer cares about.  An unknown gnssId
// in the payload is silently skipped.
struct UbxGnssEnables {
    bool gps;
    bool sbas;
    bool galileo;
    bool beidou;
    bool qzss;
    bool glonass;
};

// Decode a CFG-GNSS response payload (as received from the receiver
// — NOT including sync/header/checksum bytes, just the variable-
// length payload starting with msgVer).  Populates `out` with the
// per-constellation enable bits.
//
// Returns true on a structurally-valid payload; false if the length
// header doesn't match a plausible block layout or `payload` is
// null.  A successful return with all-false `out` means CFG-GNSS was
// applied but every constellation is currently disabled (which is
// itself useful diagnostic information).
bool ubx_decode_cfg_gnss_payload(const uint8_t* payload,
                                 size_t payload_len,
                                 UbxGnssEnables* out);
