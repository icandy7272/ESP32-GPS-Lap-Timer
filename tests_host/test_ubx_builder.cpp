// Host tests for src/ubx_builder.cpp — verifies the exact byte layout
// of the UBX frames we send to the u-blox M9N receiver to configure
// Automotive dynamic model, SBAS augmentation, and multi-GNSS
// concurrent tracking (2026-04-21 racing-accuracy pass).
//
// A bad UBX frame is silently ignored by the receiver, so wire-level
// verification here is the only way to catch a mistake short of
// sniffing the UART with a logic analyzer.
//
// Each test pins:
//   - Sync bytes (0xB5 0x62)
//   - Class + ID
//   - Little-endian length
//   - Specific payload bytes that encode the user-visible semantics
//   - Recomputed Fletcher-8 checksum

#include "ubx_builder.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fail_count = 0;

static void fail(const char* msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    fail_count++;
}

static void assert_eq_int(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "FAIL: %s — got %d, want %d\n", tag, got, want);
        fail_count++;
    }
}

static void assert_eq_byte(const char* tag, int index,
                           uint8_t got, uint8_t want) {
    if (got != want) {
        fprintf(stderr, "FAIL: %s[%d] — got 0x%02X, want 0x%02X\n",
                tag, index, got, want);
        fail_count++;
    }
}

// Recompute checksum over the cls..payload range and compare to the
// frame's trailing two bytes.  Frames the builder rejects as too
// small are skipped — the caller is expected to assert length first.
static void assert_checksum_valid(const char* tag,
                                  const uint8_t* frame, int len) {
    if (len < 8) {
        fail(tag);
        return;
    }
    UbxChecksum sum = ubx_compute_checksum(&frame[2], len - 4);
    if (sum.ck_a != frame[len - 2]) {
        fprintf(stderr, "FAIL: %s: ck_a got 0x%02X want 0x%02X\n",
                tag, frame[len - 2], sum.ck_a);
        fail_count++;
    }
    if (sum.ck_b != frame[len - 1]) {
        fprintf(stderr, "FAIL: %s: ck_b got 0x%02X want 0x%02X\n",
                tag, frame[len - 1], sum.ck_b);
        fail_count++;
    }
}

// --- Fletcher-8 golden-vector check -------------------------------
//
// u-blox examples / open-source tools consistently produce the same
// checksum for small inputs; we pin one here so a regression in the
// accumulator (e.g. overflow semantics) is caught independently of
// the frame builders above.
static void test_checksum_golden() {
    // Minimal UBX-CFG-NAV5-poll frame header bytes: cls=0x06, id=0x24,
    // len=0, 0.  Known reference checksum: 0x2A, 0x84.
    uint8_t data[4] = { 0x06, 0x24, 0x00, 0x00 };
    UbxChecksum sum = ubx_compute_checksum(data, sizeof(data));
    assert_eq_byte("checksum_golden_cka", 0, sum.ck_a, 0x2A);
    assert_eq_byte("checksum_golden_ckb", 0, sum.ck_b, 0x84);
}

// --- Zero-length payload frame -----------------------------------
static void test_empty_payload_frame() {
    uint8_t frame[8] = {};
    int n = ubx_build_frame(0x06, 0x24, nullptr, 0, frame, sizeof(frame));
    assert_eq_int("empty_payload_len", n, 8);
    assert_eq_byte("empty_payload_sync1", 0, frame[0], 0xB5);
    assert_eq_byte("empty_payload_sync2", 1, frame[1], 0x62);
    assert_eq_byte("empty_payload_cls",   2, frame[2], 0x06);
    assert_eq_byte("empty_payload_id",    3, frame[3], 0x24);
    assert_eq_byte("empty_payload_len_lo",4, frame[4], 0x00);
    assert_eq_byte("empty_payload_len_hi",5, frame[5], 0x00);
    assert_checksum_valid("empty_payload_checksum", frame, n);
}

// --- Buffer-too-small must be rejected cleanly --------------------
static void test_buffer_overflow_rejected() {
    uint8_t frame[5];
    int n = ubx_build_frame(0x06, 0x24, nullptr, 0, frame, sizeof(frame));
    assert_eq_int("overflow_returns_neg1", n, -1);

    // Likewise a non-null payload into a too-small buffer.
    uint8_t payload[4] = {1, 2, 3, 4};
    n = ubx_build_frame(0x06, 0x24, payload, sizeof(payload),
                        frame, sizeof(frame));
    assert_eq_int("overflow_with_payload_neg1", n, -1);

    // Null payload with non-zero len is illegal.
    uint8_t bigbuf[64];
    n = ubx_build_frame(0x06, 0x24, nullptr, 4, bigbuf, sizeof(bigbuf));
    assert_eq_int("null_payload_positive_len_neg1", n, -1);
}

// --- CFG-NAV5 Automotive ------------------------------------------
static void test_cfg_nav5_automotive() {
    uint8_t frame[64] = {};
    int n = ubx_build_cfg_nav5_automotive(frame, sizeof(frame));
    assert_eq_int("nav5_frame_len", n, 44);

    assert_eq_byte("nav5", 0, frame[0], 0xB5);
    assert_eq_byte("nav5", 1, frame[1], 0x62);
    assert_eq_byte("nav5_cls",    2, frame[2], 0x06);
    assert_eq_byte("nav5_id",     3, frame[3], 0x24);
    assert_eq_byte("nav5_len_lo", 4, frame[4], 0x24);  // 36 = 0x24
    assert_eq_byte("nav5_len_hi", 5, frame[5], 0x00);

    // Payload starts at frame[6].
    // payload[0..1] = mask (little-endian u16).  We set only bit 0.
    assert_eq_byte("nav5_mask_lo", 6, frame[6], 0x01);
    assert_eq_byte("nav5_mask_hi", 7, frame[7], 0x00);
    // payload[2] = dynModel = 4 (Automotive)
    assert_eq_byte("nav5_dynModel", 8, frame[8], 0x04);

    // All other NAV5 bytes should be zero — that is the "don't apply"
    // sentinel the u-blox receiver reads via the mask bits.
    for (int i = 9; i < 6 + 36; i++) {
        if (frame[i] != 0x00) {
            assert_eq_byte("nav5_trailing_zero", i, frame[i], 0x00);
        }
    }
    assert_checksum_valid("nav5_checksum", frame, n);

    // Too-small buffer must report -1 without writing.
    uint8_t tiny[43] = {};
    int n2 = ubx_build_cfg_nav5_automotive(tiny, sizeof(tiny));
    assert_eq_int("nav5_tiny_buf_rejected", n2, -1);
}

// --- CFG-SBAS enable -----------------------------------------------
static void test_cfg_sbas_enable() {
    uint8_t frame[32] = {};
    int n = ubx_build_cfg_sbas_enable(frame, sizeof(frame));
    assert_eq_int("sbas_frame_len", n, 16);

    assert_eq_byte("sbas_sync1",  0, frame[0], 0xB5);
    assert_eq_byte("sbas_sync2",  1, frame[1], 0x62);
    assert_eq_byte("sbas_cls",    2, frame[2], 0x06);
    assert_eq_byte("sbas_id",     3, frame[3], 0x16);
    assert_eq_byte("sbas_len_lo", 4, frame[4], 0x08);  // 8-byte payload
    assert_eq_byte("sbas_len_hi", 5, frame[5], 0x00);

    // payload[0] = mode (bit0 = enabled)
    assert_eq_byte("sbas_mode",    6, frame[6], 0x01);
    // payload[1] = usage (bit0 = range, bit1 = diffCorr)
    assert_eq_byte("sbas_usage",   7, frame[7], 0x03);
    // payload[2] = maxSBAS
    assert_eq_byte("sbas_maxSBAS", 8, frame[8], 0x03);
    // payload[3] reserved
    assert_eq_byte("sbas_reserved", 9, frame[9], 0x00);
    // payload[4..7] scanmode1 = 0 (auto)
    for (int i = 10; i < 14; i++) {
        assert_eq_byte("sbas_scan", i, frame[i], 0x00);
    }
    assert_checksum_valid("sbas_checksum", frame, n);
}

// --- CFG-GNSS multi-constellation ---------------------------------
static void test_cfg_gnss_all() {
    uint8_t frame[96] = {};
    int n = ubx_build_cfg_gnss_all(frame, sizeof(frame));
    assert_eq_int("gnss_frame_len", n, 60);

    assert_eq_byte("gnss_sync1",  0, frame[0], 0xB5);
    assert_eq_byte("gnss_sync2",  1, frame[1], 0x62);
    assert_eq_byte("gnss_cls",    2, frame[2], 0x06);
    assert_eq_byte("gnss_id",     3, frame[3], 0x3E);
    assert_eq_byte("gnss_len_lo", 4, frame[4], 0x34);  // 52-byte payload
    assert_eq_byte("gnss_len_hi", 5, frame[5], 0x00);

    // payload[0..3] = header
    assert_eq_byte("gnss_msgVer",     6, frame[6], 0x00);
    assert_eq_byte("gnss_numTrkChHw", 7, frame[7], 0x00);
    assert_eq_byte("gnss_numTrkChUse",8, frame[8], 0xFF);
    assert_eq_byte("gnss_numBlocks",  9, frame[9], 0x06);

    // Each of the 6 blocks must have its enable bit (bit 0 of flags)
    // set AND a valid sigCfgMask (bits 16-23) with no stray bits in
    // between.  A previous QZSS block set bit 2 by mistake
    // (0x00010005 instead of the L1C/A-only 0x00010001), which u-blox
    // strictly validates and NAKs by rejecting the entire CFG-GNSS
    // message — silently reverting all constellations to factory
    // defaults.  Pinning the exact flag word per block catches that
    // class of bug at host test time.
    //
    // Block layout starts at offset 10 (6 sync+header + 4 gnss-header).
    constexpr int FIRST_BLOCK = 10;
    constexpr int BLOCK = 8;
    constexpr uint8_t EXPECTED_GNSS_IDS[6] = { 0, 1, 2, 3, 5, 6 };
    // Per-block expected flag word = (sigCfgMask << 16) | 0x01.  For
    // M9N L1-only all six constellations use sigCfgMask=0x01, giving
    // the same 0x00010001 everywhere.  Any block diverging from this
    // pattern must be justified with a comment in ubx_builder.cpp.
    constexpr uint32_t EXPECTED_FLAGS[6] = {
        0x00010001,  // GPS L1C/A
        0x00010001,  // SBAS L1C/A
        0x00010001,  // Galileo E1
        0x00010001,  // BeiDou B1I
        0x00010001,  // QZSS L1C/A
        0x00010001,  // GLONASS L1OF
    };
    // Per-block reserved/max tracking channels.  u-blox strictly
    // validates these against M9N's 32-channel engine: the sum of
    // resTrkCh across all enabled blocks MUST be <= 32 or the whole
    // CFG-GNSS message is NAKed (same failure mode as the QZSS
    // sigCfgMask bug from 2026-04-21).  Pinning the exact bytes here
    // so any future edit that pushes the total over budget fails a
    // host test instead of a walking test.  (codex review
    // 2026-04-22 MEDIUM: the old test pinned only IDs and flags.)
    constexpr uint8_t EXPECTED_RES_TRK[6] = { 8, 1, 4, 8, 0, 8 };
    constexpr uint8_t EXPECTED_MAX_TRK[6] = { 16, 3, 8, 16, 3, 14 };
    int res_trk_sum = 0;
    for (int b = 0; b < 6; b++) {
        int base = FIRST_BLOCK + b * BLOCK;
        assert_eq_byte("gnss_block_id", base, frame[base], EXPECTED_GNSS_IDS[b]);
        assert_eq_byte("gnss_block_resTrkCh",
                       base + 1, frame[base + 1], EXPECTED_RES_TRK[b]);
        assert_eq_byte("gnss_block_maxTrkCh",
                       base + 2, frame[base + 2], EXPECTED_MAX_TRK[b]);
        assert_eq_byte("gnss_block_reserved1",
                       base + 3, frame[base + 3], 0x00);
        res_trk_sum += EXPECTED_RES_TRK[b];
        uint32_t got_flags =
            static_cast<uint32_t>(frame[base + 4])
            | (static_cast<uint32_t>(frame[base + 5]) << 8)
            | (static_cast<uint32_t>(frame[base + 6]) << 16)
            | (static_cast<uint32_t>(frame[base + 7]) << 24);
        if (got_flags != EXPECTED_FLAGS[b]) {
            fprintf(stderr,
                    "FAIL: gnss_block%d flags — got 0x%08X, want 0x%08X\n",
                    b, got_flags, EXPECTED_FLAGS[b]);
            fail_count++;
        }
    }

    // Enforce the M9N 32-channel reservation budget.  29 is the
    // current total; anything over 32 is a receiver-NAK trap.
    if (res_trk_sum > 32) {
        fprintf(stderr,
                "FAIL: gnss resTrkCh sum %d exceeds M9N 32-channel "
                "engine budget — u-blox will NAK CFG-GNSS\n",
                res_trk_sum);
        fail_count++;
    }

    // Specifically check Galileo (block 2) and BeiDou (block 3) are
    // present — these are the two most likely to be off in factory
    // default on M9N modules.
    assert_eq_byte("gnss_galileo_id",  FIRST_BLOCK + 2*BLOCK + 0, frame[26], 0x02);
    assert_eq_byte("gnss_beidou_id",   FIRST_BLOCK + 3*BLOCK + 0, frame[34], 0x03);

    assert_checksum_valid("gnss_checksum", frame, n);

    // Too-small buffer: 59 bytes (one less than minimum).
    uint8_t tiny[59] = {};
    int n2 = ubx_build_cfg_gnss_all(tiny, sizeof(tiny));
    assert_eq_int("gnss_tiny_buf_rejected", n2, -1);
}

// --- CFG-GNSS poll is an empty-payload frame ----------------------
static void test_cfg_gnss_poll() {
    uint8_t frame[16] = {};
    int n = ubx_build_cfg_gnss_poll(frame, sizeof(frame));
    assert_eq_int("gnss_poll_frame_len", n, 8);
    assert_eq_byte("gnss_poll_sync1", 0, frame[0], 0xB5);
    assert_eq_byte("gnss_poll_sync2", 1, frame[1], 0x62);
    assert_eq_byte("gnss_poll_cls",   2, frame[2], 0x06);
    assert_eq_byte("gnss_poll_id",    3, frame[3], 0x3E);
    assert_eq_byte("gnss_poll_len_lo",4, frame[4], 0x00);
    assert_eq_byte("gnss_poll_len_hi",5, frame[5], 0x00);
    assert_checksum_valid("gnss_poll_checksum", frame, n);
}

// --- CFG-GNSS response decoder ------------------------------------
static void test_decode_cfg_gnss_all_enabled() {
    // Build a response payload matching the "all 6 enabled" we send.
    uint8_t payload[4 + 6 * 8] = {};
    payload[0] = 0x00;
    payload[1] = 0x00;
    payload[2] = 0xFF;
    payload[3] = 6;
    auto set_block = [&](int idx, uint8_t gnss_id, uint32_t flags) {
        int base = 4 + idx * 8;
        payload[base + 0] = gnss_id;
        payload[base + 4] = static_cast<uint8_t>(flags);
        payload[base + 5] = static_cast<uint8_t>(flags >> 8);
        payload[base + 6] = static_cast<uint8_t>(flags >> 16);
        payload[base + 7] = static_cast<uint8_t>(flags >> 24);
    };
    set_block(0, 0, 0x00010001);
    set_block(1, 1, 0x00010001);
    set_block(2, 2, 0x00010001);
    set_block(3, 3, 0x00010001);
    set_block(4, 5, 0x00010001);
    set_block(5, 6, 0x00010001);

    UbxGnssEnables out = {};
    bool ok = ubx_decode_cfg_gnss_payload(payload, sizeof(payload), &out);
    assert_eq_int("decode_all_enabled_ok", ok ? 1 : 0, 1);
    if (!out.gps)     fail("decode_all_enabled GPS should be true");
    if (!out.sbas)    fail("decode_all_enabled SBAS should be true");
    if (!out.galileo) fail("decode_all_enabled Galileo should be true");
    if (!out.beidou)  fail("decode_all_enabled BeiDou should be true");
    if (!out.qzss)    fail("decode_all_enabled QZSS should be true");
    if (!out.glonass) fail("decode_all_enabled GLONASS should be true");
}

static void test_decode_cfg_gnss_factory_default() {
    // Simulate the failure mode from the 2026-04-21 walking test:
    // CFG-GNSS was NAKed, receiver reverted to GPS+GLONASS only.
    // Decoder must report Galileo/BeiDou as disabled so the operator
    // (or live_map) knows the config never took effect.
    uint8_t payload[4 + 6 * 8] = {};
    payload[3] = 6;
    auto set_block = [&](int idx, uint8_t gnss_id, bool enabled) {
        int base = 4 + idx * 8;
        payload[base + 0] = gnss_id;
        payload[base + 4] = enabled ? 0x01 : 0x00;
    };
    set_block(0, 0, true);   // GPS
    set_block(1, 1, true);   // SBAS
    set_block(2, 2, false);  // Galileo OFF
    set_block(3, 3, false);  // BeiDou OFF
    set_block(4, 5, false);  // QZSS OFF
    set_block(5, 6, true);   // GLONASS

    UbxGnssEnables out = {};
    bool ok = ubx_decode_cfg_gnss_payload(payload, sizeof(payload), &out);
    assert_eq_int("decode_factory_ok", ok ? 1 : 0, 1);
    if (!out.gps)     fail("decode_factory GPS should be true");
    if (!out.glonass) fail("decode_factory GLONASS should be true");
    if (out.galileo)  fail("decode_factory Galileo should be false (NAK revert)");
    if (out.beidou)   fail("decode_factory BeiDou should be false (NAK revert)");
    if (out.qzss)     fail("decode_factory QZSS should be false");
}

static void test_decode_cfg_gnss_rejects_truncated() {
    // Payload claims 6 blocks but is too short to contain them.
    uint8_t payload[6] = {0x00, 0x00, 0xFF, 6, 0x00, 0x00};
    UbxGnssEnables out = {};
    bool ok = ubx_decode_cfg_gnss_payload(payload, sizeof(payload), &out);
    assert_eq_int("decode_truncated_rejected", ok ? 1 : 0, 0);

    // Null input is also rejected cleanly.
    ok = ubx_decode_cfg_gnss_payload(nullptr, 100, &out);
    assert_eq_int("decode_null_payload_rejected", ok ? 1 : 0, 0);
}

int main() {
    test_checksum_golden();
    test_empty_payload_frame();
    test_buffer_overflow_rejected();
    test_cfg_nav5_automotive();
    test_cfg_sbas_enable();
    test_cfg_gnss_all();
    test_cfg_gnss_poll();
    test_decode_cfg_gnss_all_enabled();
    test_decode_cfg_gnss_factory_default();
    test_decode_cfg_gnss_rejects_truncated();

    if (fail_count > 0) {
        fprintf(stderr, "test_ubx_builder: %d FAILURE(S)\n", fail_count);
        return fail_count;
    }
    printf("test_ubx_builder: OK\n");
    return 0;
}
