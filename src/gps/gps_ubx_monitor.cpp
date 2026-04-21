#include "gps/gps_ubx_monitor.h"

#include "ubx_parser.h"

#include <Arduino.h>

namespace {

// Single process-wide parser.  gps_uart_init() runs on the main task
// before gps_task starts, so there is never concurrent access to
// this state — sharing one instance between boot-time drain and
// runtime gps_feed_char keeps context around across the transition.
UbxAckParser s_parser;

// Count of NAKs observed since last reset.  Exposed via logs only
// (no accessor) so a downstream tool like live_map could parse the
// `[gps-ubx] NAK ...` lines and surface the number of rejected
// configs; we avoid an extra API surface here.
uint32_t s_nak_count = 0;

// Structured UBX-message-name decoder declared in ubx_parser.h —
// duplicate forward declaration avoided by including the header.

void log_event(UbxEvent ev, UbxAckInfo info) {
    const char* name = ubx_message_name(info.cls_id, info.msg_id);
    switch (ev) {
    case UbxEvent::Ack:
        Serial.printf("[gps-ubx] ACK %s (cls=0x%02X id=0x%02X)\n",
                      name, info.cls_id, info.msg_id);
        break;
    case UbxEvent::Nak:
        s_nak_count++;
        Serial.printf("[gps-ubx] NAK %s (cls=0x%02X id=0x%02X) "
                      "— receiver rejected this configuration; "
                      "nak_count=%u\n",
                      name, info.cls_id, info.msg_id,
                      static_cast<unsigned>(s_nak_count));
        break;
    case UbxEvent::ChecksumError:
        Serial.println("[gps-ubx] WARN: UBX frame with bad checksum "
                       "(UART corruption or wrong baud?)");
        break;
    case UbxEvent::None:
    default:
        break;
    }
}

}  // namespace

void gps_ubx_monitor_reset() {
    s_parser.reset();
    s_nak_count = 0;
}

void gps_ubx_monitor_feed_byte(uint8_t byte) {
    UbxEvent ev = s_parser.feed(byte);
    if (ev != UbxEvent::None) {
        log_event(ev, s_parser.last_ack());
    }
}

bool gps_ubx_monitor_drain_for_ack(uint8_t expect_cls,
                                   uint8_t expect_id,
                                   uint32_t timeout_ms) {
    uint32_t start = millis();
    bool matched = false;
    while ((millis() - start) < timeout_ms) {
        while (Serial2.available() > 0) {
            int c = Serial2.read();
            if (c < 0) {
                break;
            }
            UbxEvent ev = s_parser.feed(static_cast<uint8_t>(c));
            if (ev != UbxEvent::None) {
                UbxAckInfo info = s_parser.last_ack();
                log_event(ev, info);
                if ((ev == UbxEvent::Ack || ev == UbxEvent::Nak)
                    && info.cls_id == expect_cls
                    && info.msg_id == expect_id) {
                    matched = true;
                    // Don't return early — continue draining so any
                    // back-to-back ACKs get logged before the caller
                    // sends the next CFG.  Extra ~5-20 ms is cheap.
                }
            }
        }
        if (matched) {
            // One more short sweep to empty the buffer, then exit.
            // A typical receiver emits ACK within ~5 ms, so a 20 ms
            // tail is enough to catch any trailing ACKs from earlier
            // batched configs.
            delay(20);
            while (Serial2.available() > 0) {
                int c = Serial2.read();
                if (c < 0) break;
                UbxEvent ev = s_parser.feed(static_cast<uint8_t>(c));
                if (ev != UbxEvent::None) {
                    log_event(ev, s_parser.last_ack());
                }
            }
            return true;
        }
        delay(5);
    }
    Serial.printf("[gps-ubx] WARN: no ACK/NAK for cls=0x%02X id=0x%02X "
                  "within %u ms — config may not have taken effect\n",
                  expect_cls, expect_id, static_cast<unsigned>(timeout_ms));
    return false;
}
