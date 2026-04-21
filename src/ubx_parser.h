#pragma once

// ============================================================
// Tiny UBX frame state machine focused on UBX-ACK-ACK / UBX-ACK-NAK.
//
// u-blox receivers reply to every CFG message with ACK-ACK (0x05
// 0x01) or ACK-NAK (0x05 0x00), where the 2-byte payload carries
// the cls/id of the original request.  Without listening for these,
// we have no way to know whether a CFG write was accepted or
// silently discarded — which is exactly the failure mode the
// 2026-04-21 walking test hit (CFG-GNSS NAKed, whole multi-GNSS
// config silently reverted to factory default, sats/HDOP did not
// improve).
//
// The parser is a pure byte-at-a-time state machine with no dynamic
// allocation, no Arduino / FreeRTOS dependency, and a fixed-size
// 2-byte payload buffer (since ACK frames are always exactly 2
// bytes long; any longer UBX frame is consumed by the state machine
// and discarded without allocating).  Runs in parallel with the
// NMEA parser on the same byte stream: NMEA text (0x20-0x7E + CR/LF)
// never triggers the 0xB5 sync byte, so feeding both parsers the
// same bytes is safe.
// ============================================================

#include <stdint.h>

enum class UbxEvent : uint8_t {
    // Byte consumed, no terminal event yet (most common).
    None,
    // A valid UBX-ACK-ACK frame completed: last_ack() returns the
    // cls/id that the receiver is acknowledging.
    Ack,
    // A valid UBX-ACK-NAK frame completed: last_ack() returns the
    // cls/id the receiver refused.
    Nak,
    // A fully-framed UBX message arrived with a bad Fletcher-8
    // checksum.  Indicates UART-level corruption — callers usually
    // just log and continue.
    ChecksumError,
};

struct UbxAckInfo {
    uint8_t cls_id;
    uint8_t msg_id;
};

class UbxAckParser {
public:
    // Feed one UART byte into the state machine and learn if a
    // terminal event fired.  Safe to call with arbitrary bytes
    // (including NMEA ASCII) — they fall through the idle state
    // silently until the next 0xB5 sync byte.
    UbxEvent feed(uint8_t byte);

    // Valid only immediately after feed() returned Ack or Nak; the
    // cls/id of the CFG message the receiver is acknowledging.
    UbxAckInfo last_ack() const { return last_ack_; }

    // Reset the state machine — useful after a detected reboot so a
    // half-received pre-reboot frame doesn't bleed into the new
    // receive stream.
    void reset();

private:
    enum class St : uint8_t {
        Idle,
        Sync2,
        Class,
        Id,
        LenLo,
        LenHi,
        Payload,
        CkA,
        CkB,
    };

    St       state_ = St::Idle;
    uint8_t  cls_ = 0;
    uint8_t  id_ = 0;
    uint16_t len_ = 0;
    uint16_t payload_idx_ = 0;
    uint8_t  payload_[2] = {0, 0};
    uint8_t  ck_received_a_ = 0;
    uint8_t  ck_running_a_ = 0;
    uint8_t  ck_running_b_ = 0;
    UbxAckInfo last_ack_ = {0, 0};
};

// Decode a (cls, id) pair into a short human-readable name
// (e.g. "CFG-NAV5", "CFG-GNSS").  Returns a pointer to a static
// string that lives for the lifetime of the process.  Unknown
// (cls, id) pairs return "UNKNOWN".  Used by the firmware to log
// readable [gps-ubx] ACK / NAK lines.
const char* ubx_message_name(uint8_t cls_id, uint8_t msg_id);
