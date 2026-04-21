// Host tests for src/ubx_parser.cpp — the UBX ACK/NAK state machine
// that runs in parallel with the NMEA parser to verify each CFG-*
// message sent to the u-blox receiver was actually accepted.
//
// The 2026-04-21 walking test surfaced the failure mode this parser
// exists to catch: a bad bit in CFG-GNSS flags silently NAKed the
// whole message, reverting all six constellations to factory
// defaults.  Without ACK/NAK monitoring, the boot log looked
// identical between a working and a broken config.

#include "ubx_parser.h"

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

static void assert_eq_uint(const char* tag, unsigned got, unsigned want) {
    if (got != want) {
        fprintf(stderr, "FAIL: %s — got 0x%X, want 0x%X\n", tag, got, want);
        fail_count++;
    }
}

// Compute the Fletcher-8 checksum over (cls, id, len_lo, len_hi,
// payload...) — lifted from the builder's algorithm, duplicated here
// so the tests stay independent of src/ubx_builder.cpp.
static void fletcher8(const uint8_t* data, size_t len,
                      uint8_t* a_out, uint8_t* b_out) {
    uint8_t a = 0;
    uint8_t b = 0;
    for (size_t i = 0; i < len; i++) {
        a = static_cast<uint8_t>(a + data[i]);
        b = static_cast<uint8_t>(b + a);
    }
    *a_out = a;
    *b_out = b;
}

// Build a minimal well-formed UBX frame into `out`.  Returns bytes
// written.  Caller supplies `payload` and `payload_len`; we add the
// sync bytes, header, and checksum.  Used below to construct
// synthetic ACK / NAK / arbitrary UBX frames and stream them through
// the parser byte-by-byte.
static size_t make_frame(uint8_t cls, uint8_t id,
                         const uint8_t* payload, uint16_t payload_len,
                         uint8_t* out) {
    out[0] = 0xB5;
    out[1] = 0x62;
    out[2] = cls;
    out[3] = id;
    out[4] = static_cast<uint8_t>(payload_len & 0xFF);
    out[5] = static_cast<uint8_t>((payload_len >> 8) & 0xFF);
    for (uint16_t i = 0; i < payload_len; i++) {
        out[6 + i] = payload[i];
    }
    uint8_t a;
    uint8_t b;
    fletcher8(&out[2], 4 + payload_len, &a, &b);
    out[6 + payload_len] = a;
    out[6 + payload_len + 1] = b;
    return 6 + payload_len + 2;
}

// Stream a pre-built frame through the parser one byte at a time and
// return the terminal event (the last non-None event observed).
static UbxEvent stream_frame(UbxAckParser& parser,
                             const uint8_t* frame, size_t len) {
    UbxEvent terminal = UbxEvent::None;
    for (size_t i = 0; i < len; i++) {
        UbxEvent ev = parser.feed(frame[i]);
        if (ev != UbxEvent::None) {
            terminal = ev;
        }
    }
    return terminal;
}

// --- Valid UBX-ACK-ACK round-trips ---------------------------------
static void test_ack_ack_for_cfg_nav5() {
    UbxAckParser parser;
    uint8_t payload[2] = { 0x06, 0x24 };  // CFG-NAV5
    uint8_t frame[16];
    size_t n = make_frame(0x05, 0x01, payload, 2, frame);
    UbxEvent ev = stream_frame(parser, frame, n);
    assert_eq_int("ack_nav5 event", static_cast<int>(ev),
                  static_cast<int>(UbxEvent::Ack));
    UbxAckInfo ack = parser.last_ack();
    assert_eq_uint("ack_nav5 cls_id", ack.cls_id, 0x06);
    assert_eq_uint("ack_nav5 msg_id", ack.msg_id, 0x24);
}

static void test_ack_ack_for_cfg_gnss() {
    UbxAckParser parser;
    uint8_t payload[2] = { 0x06, 0x3E };
    uint8_t frame[16];
    size_t n = make_frame(0x05, 0x01, payload, 2, frame);
    UbxEvent ev = stream_frame(parser, frame, n);
    assert_eq_int("ack_gnss event", static_cast<int>(ev),
                  static_cast<int>(UbxEvent::Ack));
    UbxAckInfo ack = parser.last_ack();
    assert_eq_uint("ack_gnss cls_id", ack.cls_id, 0x06);
    assert_eq_uint("ack_gnss msg_id", ack.msg_id, 0x3E);
}

// --- Valid UBX-ACK-NAK is a distinct event ------------------------
static void test_nak_for_cfg_gnss() {
    UbxAckParser parser;
    uint8_t payload[2] = { 0x06, 0x3E };
    uint8_t frame[16];
    size_t n = make_frame(0x05, 0x00, payload, 2, frame);
    UbxEvent ev = stream_frame(parser, frame, n);
    assert_eq_int("nak_gnss event", static_cast<int>(ev),
                  static_cast<int>(UbxEvent::Nak));
    UbxAckInfo ack = parser.last_ack();
    assert_eq_uint("nak_gnss cls_id", ack.cls_id, 0x06);
    assert_eq_uint("nak_gnss msg_id", ack.msg_id, 0x3E);
}

// --- Bad checksum produces ChecksumError, not a silent ignore -----
static void test_bad_checksum_detected() {
    UbxAckParser parser;
    uint8_t payload[2] = { 0x06, 0x24 };
    uint8_t frame[16];
    size_t n = make_frame(0x05, 0x01, payload, 2, frame);
    // Corrupt the second-to-last byte (ck_a) without recomputing.
    frame[n - 2] ^= 0xFF;
    UbxEvent ev = stream_frame(parser, frame, n);
    assert_eq_int("bad_checksum event", static_cast<int>(ev),
                  static_cast<int>(UbxEvent::ChecksumError));
}

// --- NMEA-style bytes are silently discarded and do not crash -----
// The parser runs in parallel with the NMEA one; it must chew the
// ASCII byte stream between UBX frames without emitting events or
// mistracking its state machine.
static void test_nmea_bytes_pass_through() {
    UbxAckParser parser;
    const char* nmea = "$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
    for (size_t i = 0; nmea[i] != '\0'; i++) {
        UbxEvent ev = parser.feed(static_cast<uint8_t>(nmea[i]));
        if (ev != UbxEvent::None) {
            fail("NMEA byte stream emitted a non-None event");
            return;
        }
    }
    // After the NMEA stream, a fresh UBX-ACK frame should still
    // parse cleanly — state was not corrupted.
    uint8_t payload[2] = { 0x06, 0x16 };
    uint8_t frame[16];
    size_t n = make_frame(0x05, 0x01, payload, 2, frame);
    UbxEvent ev = stream_frame(parser, frame, n);
    assert_eq_int("post-nmea ack event", static_cast<int>(ev),
                  static_cast<int>(UbxEvent::Ack));
}

// --- Mid-frame noise recovers cleanly at next sync -----------------
// Simulates a UART glitch by injecting random bytes in the middle of
// what looks like the start of a UBX frame, then sends a clean frame.
static void test_mid_frame_recovery() {
    UbxAckParser parser;
    // Broken pre-amble: sync-like bytes but missing SYNC2.
    const uint8_t junk[] = { 0xB5, 0x00, 0xB5, 0xFF, 0xB5, 0xB5 };
    for (uint8_t b : junk) {
        (void)parser.feed(b);
    }
    uint8_t payload[2] = { 0x06, 0x24 };
    uint8_t frame[16];
    size_t n = make_frame(0x05, 0x01, payload, 2, frame);
    UbxEvent ev = stream_frame(parser, frame, n);
    assert_eq_int("recovery ack event", static_cast<int>(ev),
                  static_cast<int>(UbxEvent::Ack));
}

// --- Long non-ACK UBX frame is consumed without triggering events -
// e.g. a UBX-NAV-PVT or similar — our parser should not emit events
// for it and must not crash on the longer payload.
static void test_long_frame_consumed() {
    UbxAckParser parser;
    uint8_t payload[92];  // typical NAV-PVT is 92 bytes
    for (size_t i = 0; i < sizeof(payload); i++) payload[i] = static_cast<uint8_t>(i);
    uint8_t frame[128];
    size_t n = make_frame(0x01, 0x07, payload, sizeof(payload), frame);
    UbxEvent ev = stream_frame(parser, frame, n);
    assert_eq_int("long frame event", static_cast<int>(ev),
                  static_cast<int>(UbxEvent::None));

    // Next ACK must still work.
    uint8_t ack_payload[2] = { 0x06, 0x3E };
    uint8_t ack_frame[16];
    size_t m = make_frame(0x05, 0x01, ack_payload, 2, ack_frame);
    UbxEvent ev2 = stream_frame(parser, ack_frame, m);
    assert_eq_int("post-long ack event", static_cast<int>(ev2),
                  static_cast<int>(UbxEvent::Ack));
}

// --- reset() wipes partial-frame state --------------------------
static void test_reset_aborts_in_flight_frame() {
    UbxAckParser parser;
    // Feed the first 4 bytes of a valid ACK frame, then reset.
    uint8_t payload[2] = { 0x06, 0x24 };
    uint8_t frame[16];
    size_t n = make_frame(0x05, 0x01, payload, 2, frame);
    for (int i = 0; i < 4; i++) (void)parser.feed(frame[i]);
    parser.reset();
    // Now feed the remaining bytes — they should be consumed as
    // noise (state is Idle, first byte is 0x02 which != 0xB5).
    for (size_t i = 4; i < n; i++) {
        UbxEvent ev = parser.feed(frame[i]);
        if (ev != UbxEvent::None) {
            fail("reset() did not abort partial frame cleanly");
            return;
        }
    }
}

// --- Message-name decoding spot-checks --------------------------
static void test_message_name_lookup() {
    if (strcmp(ubx_message_name(0x06, 0x24), "CFG-NAV5") != 0) {
        fail("ubx_message_name CFG-NAV5");
    }
    if (strcmp(ubx_message_name(0x06, 0x3E), "CFG-GNSS") != 0) {
        fail("ubx_message_name CFG-GNSS");
    }
    if (strcmp(ubx_message_name(0x06, 0x16), "CFG-SBAS") != 0) {
        fail("ubx_message_name CFG-SBAS");
    }
    if (strcmp(ubx_message_name(0x06, 0x99), "CFG-?") != 0) {
        fail("ubx_message_name unknown CFG");
    }
    if (strcmp(ubx_message_name(0x01, 0x07), "UNKNOWN") != 0) {
        fail("ubx_message_name non-CFG class");
    }
}

int main() {
    test_ack_ack_for_cfg_nav5();
    test_ack_ack_for_cfg_gnss();
    test_nak_for_cfg_gnss();
    test_bad_checksum_detected();
    test_nmea_bytes_pass_through();
    test_mid_frame_recovery();
    test_long_frame_consumed();
    test_reset_aborts_in_flight_frame();
    test_message_name_lookup();

    if (fail_count > 0) {
        fprintf(stderr, "test_ubx_parser: %d FAILURE(S)\n", fail_count);
        return fail_count;
    }
    printf("test_ubx_parser: OK\n");
    return 0;
}
