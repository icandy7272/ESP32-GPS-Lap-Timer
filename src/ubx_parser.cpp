#include "ubx_parser.h"

#include <stddef.h>

namespace {

constexpr uint8_t SYNC1 = 0xB5;
constexpr uint8_t SYNC2 = 0x62;

// Max payload we actually buffer.  Longer frames are still fully
// consumed (state machine counts bytes against `len_`) but only the
// first 2 bytes are retained — which is exactly what ACK/NAK needs
// and what UBX-ACK-* frames always are anyway.
constexpr size_t MAX_STORED_PAYLOAD = 2;

}  // namespace

void UbxAckParser::reset() {
    state_ = St::Idle;
    cls_ = 0;
    id_ = 0;
    len_ = 0;
    payload_idx_ = 0;
    ck_received_a_ = 0;
    ck_running_a_ = 0;
    ck_running_b_ = 0;
    last_ack_ = {0, 0};
}

UbxEvent UbxAckParser::feed(uint8_t byte) {
    switch (state_) {
    case St::Idle:
        if (byte == SYNC1) {
            state_ = St::Sync2;
        }
        // Any other byte (NMEA ASCII, noise) is silently discarded.
        return UbxEvent::None;

    case St::Sync2:
        if (byte == SYNC2) {
            state_ = St::Class;
            ck_running_a_ = 0;
            ck_running_b_ = 0;
        } else if (byte == SYNC1) {
            // Stay in Sync2 so a 0xB5 0xB5 ... 0x62 sequence still
            // latches.  Rare but harmless.
        } else {
            state_ = St::Idle;
        }
        return UbxEvent::None;

    case St::Class:
        cls_ = byte;
        ck_running_a_ = static_cast<uint8_t>(ck_running_a_ + byte);
        ck_running_b_ = static_cast<uint8_t>(ck_running_b_ + ck_running_a_);
        state_ = St::Id;
        return UbxEvent::None;

    case St::Id:
        id_ = byte;
        ck_running_a_ = static_cast<uint8_t>(ck_running_a_ + byte);
        ck_running_b_ = static_cast<uint8_t>(ck_running_b_ + ck_running_a_);
        state_ = St::LenLo;
        return UbxEvent::None;

    case St::LenLo:
        len_ = byte;
        ck_running_a_ = static_cast<uint8_t>(ck_running_a_ + byte);
        ck_running_b_ = static_cast<uint8_t>(ck_running_b_ + ck_running_a_);
        state_ = St::LenHi;
        return UbxEvent::None;

    case St::LenHi:
        len_ = static_cast<uint16_t>(len_ | (static_cast<uint16_t>(byte) << 8));
        ck_running_a_ = static_cast<uint8_t>(ck_running_a_ + byte);
        ck_running_b_ = static_cast<uint8_t>(ck_running_b_ + ck_running_a_);
        payload_idx_ = 0;
        if (len_ == 0) {
            state_ = St::CkA;
        } else {
            state_ = St::Payload;
        }
        return UbxEvent::None;

    case St::Payload:
        if (payload_idx_ < MAX_STORED_PAYLOAD) {
            payload_[payload_idx_] = byte;
        }
        ck_running_a_ = static_cast<uint8_t>(ck_running_a_ + byte);
        ck_running_b_ = static_cast<uint8_t>(ck_running_b_ + ck_running_a_);
        payload_idx_++;
        if (payload_idx_ >= len_) {
            state_ = St::CkA;
        }
        return UbxEvent::None;

    case St::CkA:
        ck_received_a_ = byte;
        state_ = St::CkB;
        return UbxEvent::None;

    case St::CkB:
        // Verify checksum, then decide the terminal event.
        state_ = St::Idle;
        if (ck_received_a_ != ck_running_a_ || byte != ck_running_b_) {
            return UbxEvent::ChecksumError;
        }
        // Valid frame.  Only UBX-ACK-ACK (0x05 0x01) and
        // UBX-ACK-NAK (0x05 0x00) are interesting to this parser;
        // anything else is accepted but silently consumed.
        if (cls_ == 0x05 && len_ == 2) {
            last_ack_.cls_id = payload_[0];
            last_ack_.msg_id = payload_[1];
            if (id_ == 0x01) return UbxEvent::Ack;
            if (id_ == 0x00) return UbxEvent::Nak;
        }
        return UbxEvent::None;
    }
    // Defensive: unknown state
    state_ = St::Idle;
    return UbxEvent::None;
}

// Short human-readable names for the UBX messages we send from the
// lap timer.  Kept tiny on purpose — this table only exists so the
// boot log reads `[gps-ubx] ACK CFG-NAV5` instead of `ACK 06/24`.
const char* ubx_message_name(uint8_t cls_id, uint8_t msg_id) {
    // Class 0x06 = CFG — all the configs we send live here.
    if (cls_id == 0x06) {
        switch (msg_id) {
        case 0x00: return "CFG-PRT";
        case 0x01: return "CFG-MSG";
        case 0x08: return "CFG-RATE";
        case 0x09: return "CFG-CFG";
        case 0x16: return "CFG-SBAS";
        case 0x24: return "CFG-NAV5";
        case 0x3E: return "CFG-GNSS";
        default:   return "CFG-?";
        }
    }
    return "UNKNOWN";
}
