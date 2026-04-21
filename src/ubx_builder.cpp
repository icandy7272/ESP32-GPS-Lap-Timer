#include "ubx_builder.h"

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
    // mask bit 0 = apply dynModel; all other bits left zero so other
    // NAV5 fields keep their factory defaults.
    payload[0] = 0x01;
    payload[1] = 0x00;
    // dynModel = 4 (Automotive)
    payload[2] = 0x04;
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

    set_block(0, 0, 8, 16, 0x00010001);  // GPS L1C/A
    set_block(1, 1, 1, 3,  0x00010001);  // SBAS L1C/A
    set_block(2, 2, 4, 8,  0x00010001);  // Galileo E1
    set_block(3, 3, 8, 16, 0x00010001);  // BeiDou B1I
    set_block(4, 5, 0, 3,  0x00010005);  // QZSS L1C/A + L1S
    set_block(5, 6, 8, 14, 0x00010001);  // GLONASS L1OF

    return ubx_build_frame(0x06, 0x3E, payload, sizeof(payload), out, out_cap);
}
