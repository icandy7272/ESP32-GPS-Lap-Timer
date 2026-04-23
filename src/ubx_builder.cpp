#include "ubx_builder.h"

#include <stddef.h>
#include <string.h>

namespace {

constexpr uint8_t SYNC1 = 0xB5;
constexpr uint8_t SYNC2 = 0x62;

// Header = cls, id, len_lo, len_hi (4 bytes).  Checksum covers
// header + payload.
constexpr size_t FRAME_OVERHEAD = 2 /*sync*/ + 4 /*header*/ + 2 /*checksum*/;

}  // namespace

UbxChecksum ubx_compute_checksum(const uint8_t* data, size_t len) {
    uint8_t a = 0;
    uint8_t b = 0;
    for (size_t i = 0; i < len; i++) {
        a += data[i];
        b += a;
    }
    UbxChecksum sum;
    sum.ck_a = a;
    sum.ck_b = b;
    return sum;
}

int ubx_build_frame(uint8_t cls, uint8_t id,
                    const uint8_t* payload, uint16_t payload_len,
                    uint8_t* out, size_t out_cap) {
    if (out == nullptr) {
        return -1;
    }
    if (payload == nullptr && payload_len > 0) {
        return -1;
    }
    const size_t total = FRAME_OVERHEAD + static_cast<size_t>(payload_len);
    if (out_cap < total) {
        return -1;
    }

    out[0] = SYNC1;
    out[1] = SYNC2;
    out[2] = cls;
    out[3] = id;
    out[4] = static_cast<uint8_t>(payload_len & 0xFF);
    out[5] = static_cast<uint8_t>((payload_len >> 8) & 0xFF);

    if (payload_len > 0) {
        memcpy(&out[6], payload, payload_len);
    }

    // Checksum covers header (cls, id, len) + payload — i.e. out[2..6+payload_len).
    const UbxChecksum sum = ubx_compute_checksum(&out[2], 4 + payload_len);
    out[6 + payload_len]     = sum.ck_a;
    out[6 + payload_len + 1] = sum.ck_b;

    return static_cast<int>(total);
}

int ubx_build_cfg_nav5_automotive(uint8_t* out, size_t out_cap) {
    uint8_t payload[36] = {};
    // Apply THREE fields in one frame (mask bits 0, 1, and 6):
    //   bit 0 = dynModel       → Automotive motion model
    //   bit 1 = minEl          → minimum elevation for sat acceptance
    //   bit 6 = staticHoldMask → staticHoldThresh + staticHoldMaxDist
    //
    // 0x01 | 0x02 | 0x40 = 0x43.  All other NAV5 fields stay on
    // factory defaults because their mask bits are zero.  The 2026-
    // 04-23 walking-test report showed visible position jitter that
    // the default 5° minimum elevation and disabled static-hold
    // allowed through; these two knobs suppress it without touching
    // the existing 6-constellation / SBAS / HDOP-gate setup.
    payload[0] = 0x43;
    payload[1] = 0x00;

    // dynModel = 4 (Automotive).  Unchanged.
    payload[2] = 0x04;

    // minElev = 10°.  Default is 5°; satellites below 10° are
    // dominated by multipath (reflections off buildings, trees,
    // car bodies) which inject lateral noise into the fix.  Under
    // a 6-constellation sky we have 30+ visible satellites at any
    // moment, so cutting the low-elevation tail still leaves 15-20
    // high-quality signals — HDOP typically improves rather than
    // degrades.  Stored as int8_t at payload[12].
    payload[12] = 0x0A;

    // staticHoldThresh = 5 cm/s.  Default is 0 (disabled).  When
    // the receiver's own speed estimate drops below this, it
    // "holds" the position at the last confirmed fix instead of
    // letting noise wobble it.  5 cm/s only activates when truly
    // stationary (walking pace is 100 cm/s, stopped kart is still
    // ~0-2 cm/s).  Eliminates the "parked car slowly wandering"
    // class of drift without affecting any moving-vehicle path.
    // Stored as uint8_t at payload[22].
    payload[22] = 0x05;

    // staticHoldMaxDist = 0 (speed-only release).  If we leave
    // this disabled, static hold releases as soon as speed crosses
    // back above staticHoldThresh — simplest semantics, no risk of
    // a stale-snap that doesn't release when the vehicle moves.
    // payload[28-29] stays zero.

    return ubx_build_frame(0x06, 0x24, payload, sizeof(payload), out, out_cap);
}

int ubx_build_cfg_sbas_enable(uint8_t* out, size_t out_cap) {
    uint8_t payload[8] = {};
    payload[0] = 0x01;  // mode: SBAS enabled
    payload[1] = 0x03;  // usage: range + diffCorr
    payload[2] = 0x03;  // maxSBAS: track up to 3 SBAS birds
    // payload[3] reserved = 0
    // payload[4..7] scanmode1 = 0 → auto-scan all PRNs (picks regional system)
    return ubx_build_frame(0x06, 0x16, payload, sizeof(payload), out, out_cap);
}

int ubx_build_cfg_gnss_all(uint8_t* out, size_t out_cap) {
    constexpr int NUM_BLOCKS = 6;
    constexpr int HEADER = 4;
    constexpr int BLOCK = 8;
    uint8_t payload[HEADER + NUM_BLOCKS * BLOCK] = {};

    payload[0] = 0x00;           // msgVer
    payload[1] = 0x00;           // numTrkChHw (read-only in request)
    payload[2] = 0xFF;           // numTrkChUse (0xFF = use all)
    payload[3] = NUM_BLOCKS;     // numConfigBlocks

    auto set_block = [&](int idx, uint8_t gnss_id,
                         uint8_t res_trk, uint8_t max_trk,
                         uint32_t flags) {
        int base = HEADER + idx * BLOCK;
        payload[base + 0] = gnss_id;
        payload[base + 1] = res_trk;
        payload[base + 2] = max_trk;
        payload[base + 3] = 0x00;
        payload[base + 4] = static_cast<uint8_t>(flags);
        payload[base + 5] = static_cast<uint8_t>(flags >> 8);
        payload[base + 6] = static_cast<uint8_t>(flags >> 16);
        payload[base + 7] = static_cast<uint8_t>(flags >> 24);
    };

    // Per-block flag word = (sigCfgMask << 16) | enable_bit.
    // For u-blox M9N L1-only hardware, sigCfgMask=0x01 means the
    // primary L1 signal for that constellation (GPS L1C/A, Galileo E1,
    // BeiDou B1I, GLONASS L1OF, SBAS L1C/A, QZSS L1C/A).
    //
    // HISTORY: a previous value here for QZSS was 0x00010005, which
    // set bit 2 (an undefined bit in this field) instead of the
    // intended QZSS-L1S bit 18.  u-blox strictly validates CFG-GNSS
    // and NAKs the ENTIRE message when any block's flags are invalid,
    // so the bad QZSS flag silently reverted all six constellations
    // back to the factory default (often just GPS+GLONASS) — which
    // is exactly the "sats/HDOP didn't improve" symptom observed in
    // the 2026-04-21 walking test.  Keeping QZSS to L1C/A only also
    // matches what's actually usable outside Japan.
    set_block(0, 0, 8, 16, 0x00010001);  // GPS L1C/A
    set_block(1, 1, 1, 3,  0x00010001);  // SBAS L1C/A
    set_block(2, 2, 4, 8,  0x00010001);  // Galileo E1
    set_block(3, 3, 8, 16, 0x00010001);  // BeiDou B1I
    set_block(4, 5, 0, 3,  0x00010001);  // QZSS L1C/A
    set_block(5, 6, 8, 14, 0x00010001);  // GLONASS L1OF

    return ubx_build_frame(0x06, 0x3E, payload, sizeof(payload), out, out_cap);
}

int ubx_build_cfg_gnss_poll(uint8_t* out, size_t out_cap) {
    // Polling an u-blox CFG message is done by sending the message
    // with an empty payload.  The receiver replies with a full SET-
    // style message carrying its current config.
    return ubx_build_frame(0x06, 0x3E, nullptr, 0, out, out_cap);
}

bool ubx_decode_cfg_gnss_payload(const uint8_t* payload,
                                 size_t payload_len,
                                 UbxGnssEnables* out) {
    if (payload == nullptr || out == nullptr) {
        return false;
    }
    *out = {};
    // Payload must be at least the 4-byte header.
    if (payload_len < 4) {
        return false;
    }
    uint8_t num_blocks = payload[3];
    constexpr size_t HEADER = 4;
    constexpr size_t BLOCK = 8;
    size_t required = HEADER + static_cast<size_t>(num_blocks) * BLOCK;
    if (payload_len < required) {
        return false;
    }

    for (uint8_t i = 0; i < num_blocks; i++) {
        size_t base = HEADER + static_cast<size_t>(i) * BLOCK;
        uint8_t gnss_id = payload[base + 0];
        // flags is u32 little-endian at base+4..base+7.  Bit 0 = enable.
        uint32_t flags = static_cast<uint32_t>(payload[base + 4])
                       | (static_cast<uint32_t>(payload[base + 5]) << 8)
                       | (static_cast<uint32_t>(payload[base + 6]) << 16)
                       | (static_cast<uint32_t>(payload[base + 7]) << 24);
        bool enabled = (flags & 0x01) != 0;
        switch (gnss_id) {
        case 0: out->gps     = enabled; break;
        case 1: out->sbas    = enabled; break;
        case 2: out->galileo = enabled; break;
        case 3: out->beidou  = enabled; break;
        case 5: out->qzss    = enabled; break;
        case 6: out->glonass = enabled; break;
        default: break;  // unknown gnssId, skip
        }
    }
    return true;
}
