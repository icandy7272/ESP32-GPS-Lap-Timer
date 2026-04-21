#pragma once

// Process-wide UBX ACK/NAK monitor.  A single UbxAckParser is fed
// every byte that arrives on Serial2 — bytes that form a valid
// UBX-ACK-ACK or UBX-ACK-NAK frame trigger a `[gps-ubx] ACK|NAK
// CFG-XXX (cls=... id=...)` log line, decoded by ubx_message_name().
// NMEA bytes flow through the parser silently.
//
// Two entry points:
//   - gps_ubx_monitor_feed_byte(b): feed a single byte (called from
//     gps_feed_char() so runtime NMEA streams are monitored too).
//   - gps_ubx_monitor_drain(ms): actively pull bytes from Serial2
//     for a bounded window, feed them, return true if at least one
//     ACK/NAK event decoded during the window.  Used by
//     gps_uart_init() to verify every CFG-* write immediately after
//     sending.

#include <stdint.h>

void gps_ubx_monitor_reset();
void gps_ubx_monitor_feed_byte(uint8_t byte);

// Drain bytes from Serial2 for up to `timeout_ms` milliseconds,
// feeding each byte to the UBX parser.  Returns true if we observed
// an ACK or NAK whose payload matched (expect_cls, expect_id).
// Returns false on timeout.  All observed ACK/NAK events are still
// logged whether or not they matched the expected pair — a spurious
// ACK for a different message is diagnostic info worth keeping.
bool gps_ubx_monitor_drain_for_ack(uint8_t expect_cls,
                                   uint8_t expect_id,
                                   uint32_t timeout_ms);
