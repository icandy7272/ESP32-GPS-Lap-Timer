#include "gps/gps_internal.h"

#include "config.h"
#include "gps/gps_ubx_monitor.h"
#include "pins.h"
#include "ubx_builder.h"

static void ubx_checksum(const uint8_t* data, int len,
                         uint8_t* ck_a, uint8_t* ck_b) {
    uint8_t a = 0;
    uint8_t b = 0;
    for (int i = 0; i < len; i++) {
        a += data[i];
        b += a;
    }
    *ck_a = a;
    *ck_b = b;
}

static void ubx_send(uint8_t cls, uint8_t id,
                     const uint8_t* payload, uint16_t len) {
    uint8_t header[4] = {
        cls, id,
        static_cast<uint8_t>(len & 0xFF),
        static_cast<uint8_t>((len >> 8) & 0xFF)
    };

    uint8_t ck_a = 0;
    uint8_t ck_b = 0;
    ubx_checksum(header, 4, &ck_a, &ck_b);

    for (uint16_t i = 0; i < len; i++) {
        ck_a += payload[i];
        ck_b += ck_a;
    }

    Serial2.write(0xB5);
    Serial2.write(0x62);
    Serial2.write(header, 4);
    if (len > 0 && payload != nullptr) {
        Serial2.write(payload, len);
    }
    Serial2.write(ck_a);
    Serial2.write(ck_b);
    Serial2.flush();
}

static void ubx_cfg_prt(uint32_t baud) {
    uint8_t payload[20] = {};
    payload[0] = 0x01;
    payload[4] = 0xD0;
    payload[5] = 0x08;

    payload[8]  = static_cast<uint8_t>(baud);
    payload[9]  = static_cast<uint8_t>(baud >> 8);
    payload[10] = static_cast<uint8_t>(baud >> 16);
    payload[11] = static_cast<uint8_t>(baud >> 24);

    payload[12] = 0x07;
    payload[13] = 0x00;
    payload[14] = 0x03;
    payload[15] = 0x00;

    ubx_send(0x06, 0x00, payload, sizeof(payload));
}

static void ubx_cfg_rate(uint8_t rate_hz) {
    uint16_t meas_ms = 1000 / rate_hz;
    uint8_t payload[6] = {};

    payload[0] = static_cast<uint8_t>(meas_ms & 0xFF);
    payload[1] = static_cast<uint8_t>((meas_ms >> 8) & 0xFF);
    payload[2] = 0x01;
    payload[4] = 0x01;

    ubx_send(0x06, 0x08, payload, sizeof(payload));
}

static void ubx_cfg_msg(uint8_t nmea_cls, uint8_t nmea_id, uint8_t rate) {
    uint8_t payload[3] = { nmea_cls, nmea_id, rate };
    ubx_send(0x06, 0x01, payload, sizeof(payload));
}

static void ubx_configure_nmea_sentences() {
    static constexpr uint8_t CLS = 0xF0;

    // 0x00 GGA — position fix (always on, primary source for lat/lon/HDOP/sats/height)
    ubx_cfg_msg(CLS, 0x00, 1);
    // 0x04 RMC — recommended minimum (speed, heading, UTC time/date)
    ubx_cfg_msg(CLS, 0x04, 1);
    // 0x03 GSV — satellites in view per constellation.  Enabled at a
    //           25-epoch cadence so per-constellation GSV groups arrive
    //           at ~1 Hz regardless of measurement rate.  Without this
    //           we can't tell whether Galileo/BeiDou actually locked on
    //           after CFG-GNSS was applied.  Parsed by gps_constellation.
    ubx_cfg_msg(CLS, 0x03, 25);
    // 0x02 GSA — DOP + active satellites (kept off; HDOP is read from
    //           GGA field 7 as of 2026-04-18).
    ubx_cfg_msg(CLS, 0x02, 0);
    // 0x01 GLL — redundant with GGA/RMC
    ubx_cfg_msg(CLS, 0x01, 0);
    // 0x05 VTG — velocity + track, redundant with RMC
    ubx_cfg_msg(CLS, 0x05, 0);
    // 0x08 ZDA — UTC date/time, redundant with RMC
    ubx_cfg_msg(CLS, 0x08, 0);
}

static void ubx_cfg_save() {
    uint8_t payload[13] = {};
    payload[4] = 0xFF;
    payload[5] = 0xFF;
    payload[12] = 0x07;
    ubx_send(0x06, 0x09, payload, sizeof(payload));
}

// --- Racing / accuracy config helpers ---------------------------
//
// These three functions configure the u-blox receiver for the
// accuracy the lap timer actually needs on a race track.  Each one
// builds the exact UBX frame in a small stack buffer via the pure
// builder in src/ubx_builder.cpp (testable on host), then hands
// the bytes to Serial2.
//
// See src/ubx_builder.h for a full description of each frame's
// payload layout and the rationale for Automotive / SBAS / multi-
// constellation on an M9N.
static void send_prebuilt(const uint8_t* frame, int frame_len) {
    if (frame_len <= 0) {
        return;
    }
    Serial2.write(frame, frame_len);
    Serial2.flush();
}

static void ubx_cfg_nav5_automotive() {
    uint8_t frame[44];
    int n = ubx_build_cfg_nav5_automotive(frame, sizeof(frame));
    send_prebuilt(frame, n);
}

static void ubx_cfg_sbas_enable() {
    uint8_t frame[16];
    int n = ubx_build_cfg_sbas_enable(frame, sizeof(frame));
    send_prebuilt(frame, n);
}

// One-shot poll of CFG-GNSS.  Sends an empty-payload request, reads
// bytes from Serial2 for up to `timeout_ms` looking for a UBX 0x06
// 0x3E response, decodes the per-constellation enable bits, and
// emits a `[gps-verify]` log line.  This is the ground truth — even
// if the earlier SET returned ACK, the receiver may have filtered
// some blocks (e.g. incompatible combo) and the poll response shows
// what's actually running.
//
// Side effects: consumes bytes from Serial2.  Those bytes are also
// fed through the UBX monitor so any stray ACK/NAK during the window
// is still logged.  NMEA bytes are harmless (they fall through the
// parser state machine).
//
// No-op on poll-send failure.
static void ubx_poll_cfg_gnss_and_log(uint32_t timeout_ms) {
    uint8_t poll_frame[8];
    int np = ubx_build_cfg_gnss_poll(poll_frame, sizeof(poll_frame));
    if (np <= 0) {
        Serial.println("[gps-verify] WARN: poll frame build failed");
        return;
    }
    Serial2.write(poll_frame, np);
    Serial2.flush();

    // Expected response frame: B5 62 06 3E <len_lo> <len_hi> <payload> <ck_a> <ck_b>
    // Payload length varies with num_blocks; typical 4 + 6*8 = 52 bytes.
    // Budget ~96 bytes to be safe.
    constexpr size_t MAX_RESP = 128;
    uint8_t resp[MAX_RESP];
    size_t  resp_len = 0;

    // State machine for accumulating a CFG-GNSS response while
    // coexisting with other UBX traffic.  Codex review 2026-04-22
    // caught two real bugs in the earlier version:
    //
    //   1) Non-target frames (e.g. a UBX-ACK-ACK the receiver emits
    //      for our poll itself, cls=0x05 id=0x01) used to drop us
    //      back to Idle mid-frame, so their payload bytes were then
    //      mis-scanned for new 0xB5 0x62 sync sequences.  A single
    //      UBX-ACK-ACK has 0x01 in payload byte 1 and could plausibly
    //      contain 0xB5 in another field (though not today's ACK
    //      payload, any future UBX class could) — lock-onto-garbage
    //      risk.  Fix: for non-target frames, skip by declared length
    //      so we resume sync AFTER the frame ends.
    //
    //   2) Checksum bytes on the target frame were read and
    //      discarded.  A corrupted response would silently produce
    //      bogus enable bits printed as [gps-verify] ground truth.
    //      Fix: compute Fletcher-8 over cls..payload and compare to
    //      the received ck_a/ck_b; on mismatch, WARN and continue
    //      looking for a clean frame within the remaining timeout.
    //
    // States:
    //   Idle → Sync2 → Cls → Id → LenLo → LenHi
    //     └── target (cls=0x06 id=0x3E) → Payload (with running ck)
    //     │     → CkA → CkB → validate → Done or Idle (on mismatch)
    //     └── non-target → Skip (burn declared_len + 2 chk bytes) → Idle
    enum class St {
        Idle, Sync2, Cls, Id, LenLo, LenHi,
        Payload, CkA, CkB,
        Skip,  // consume non-target frame body + checksum
        Done,
    };
    St state = St::Idle;
    uint16_t expect_len = 0;      // target frame payload length
    uint16_t got = 0;              // target frame bytes consumed
    uint8_t  cur_cls = 0;          // class of currently-scanned frame
    uint8_t  cur_id = 0;
    uint32_t skip_remaining = 0;   // bytes left to consume for non-target frame
    uint8_t  ck_a_run = 0;         // Fletcher-8 over cls..payload
    uint8_t  ck_b_run = 0;
    uint8_t  ck_a_recv = 0;
    uint32_t start = millis();
    while ((millis() - start) < timeout_ms && state != St::Done) {
        while (Serial2.available() > 0 && state != St::Done) {
            int c = Serial2.read();
            if (c < 0) break;
            uint8_t b = static_cast<uint8_t>(c);
            // Always forward to the UBX monitor — captures stray
            // ACK/NAKs that happen to arrive during our poll window.
            // Safe even for our own frame's bytes (monitor expects
            // ACK class=0x05, our CFG-GNSS is class=0x06, so the
            // monitor ignores it).
            gps_ubx_monitor_feed_byte(b);
            switch (state) {
            case St::Idle:
                if (b == 0xB5) state = St::Sync2;
                break;
            case St::Sync2:
                if (b == 0x62) {
                    state = St::Cls;
                    ck_a_run = 0;
                    ck_b_run = 0;
                } else if (b == 0xB5) {
                    // stay in Sync2 — 0xB5 0xB5 sequences latch
                } else {
                    state = St::Idle;
                }
                break;
            case St::Cls:
                cur_cls = b;
                ck_a_run = static_cast<uint8_t>(ck_a_run + b);
                ck_b_run = static_cast<uint8_t>(ck_b_run + ck_a_run);
                state = St::Id;
                break;
            case St::Id:
                cur_id = b;
                ck_a_run = static_cast<uint8_t>(ck_a_run + b);
                ck_b_run = static_cast<uint8_t>(ck_b_run + ck_a_run);
                state = St::LenLo;
                break;
            case St::LenLo:
                expect_len = b;
                ck_a_run = static_cast<uint8_t>(ck_a_run + b);
                ck_b_run = static_cast<uint8_t>(ck_b_run + ck_a_run);
                state = St::LenHi;
                break;
            case St::LenHi:
                expect_len = static_cast<uint16_t>(expect_len
                                | (static_cast<uint16_t>(b) << 8));
                ck_a_run = static_cast<uint8_t>(ck_a_run + b);
                ck_b_run = static_cast<uint8_t>(ck_b_run + ck_a_run);
                if (cur_cls == 0x06 && cur_id == 0x3E && expect_len > 0
                    && expect_len <= MAX_RESP) {
                    got = 0;
                    resp_len = 0;
                    state = St::Payload;
                } else {
                    // Non-target frame (or zero-length echo, or
                    // overlong garbage) — skip its body + checksum so
                    // we resume sync AFTER it ends.  declared_len + 2
                    // is always correct for a well-formed UBX frame.
                    skip_remaining = static_cast<uint32_t>(expect_len) + 2;
                    state = St::Skip;
                }
                break;
            case St::Payload:
                if (resp_len < MAX_RESP) {
                    resp[resp_len++] = b;
                }
                ck_a_run = static_cast<uint8_t>(ck_a_run + b);
                ck_b_run = static_cast<uint8_t>(ck_b_run + ck_a_run);
                got++;
                if (got >= expect_len) state = St::CkA;
                break;
            case St::CkA:
                ck_a_recv = b;
                state = St::CkB;
                break;
            case St::CkB:
                if (ck_a_recv == ck_a_run && b == ck_b_run) {
                    state = St::Done;
                } else {
                    // Checksum mismatch: corrupted frame (or we locked
                    // onto a false sync pattern).  Warn and resume
                    // scanning — another clean frame may arrive within
                    // the remaining timeout.
                    Serial.printf(
                        "[gps-verify] WARN: CFG-GNSS response checksum "
                        "mismatch (got %02X %02X, want %02X %02X) — "
                        "resuming scan\n",
                        ck_a_recv, b, ck_a_run, ck_b_run);
                    state = St::Idle;
                }
                break;
            case St::Skip:
                // Burn bytes belonging to a non-target frame so we
                // resume sync AFTER its checksum.
                if (skip_remaining > 0) {
                    skip_remaining--;
                }
                if (skip_remaining == 0) {
                    state = St::Idle;
                }
                break;
            case St::Done:
                break;
            }
        }
        if (state != St::Done) delay(5);
    }

    if (state != St::Done) {
        Serial.printf("[gps-verify] WARN: no CFG-GNSS poll response "
                      "within %u ms — falling back to ACK status\n",
                      static_cast<unsigned>(timeout_ms));
        return;
    }

    UbxGnssEnables en = {};
    if (!ubx_decode_cfg_gnss_payload(resp, resp_len, &en)) {
        Serial.println("[gps-verify] WARN: CFG-GNSS poll response "
                       "did not decode — malformed payload");
        return;
    }

    Serial.printf("[gps-verify] active: GPS=%c SBAS=%c GAL=%c BDS=%c "
                  "QZS=%c GLO=%c\n",
                  en.gps     ? 'Y' : 'N',
                  en.sbas    ? 'Y' : 'N',
                  en.galileo ? 'Y' : 'N',
                  en.beidou  ? 'Y' : 'N',
                  en.qzss    ? 'Y' : 'N',
                  en.glonass ? 'Y' : 'N');

    // Explicit warning when any intended constellation is missing —
    // operator would otherwise have to parse the line themselves to
    // realise the SET was partially filtered.
    if (!en.galileo || !en.beidou) {
        Serial.println("[gps-verify] WARN: expected constellations "
                       "NOT active (Galileo/BeiDou) — CFG-GNSS SET "
                       "was accepted but silently filtered; sats/HDOP "
                       "will not improve");
    }
}

static void ubx_cfg_gnss_all() {
    uint8_t frame[60];
    int n = ubx_build_cfg_gnss_all(frame, sizeof(frame));
    send_prebuilt(frame, n);
}

static bool uart_detect_nmea(int timeout_ms) {
    unsigned long start = millis();
    while ((millis() - start) < static_cast<unsigned long>(timeout_ms)) {
        if (Serial2.available() > 0) {
            if (Serial2.read() == '$') {
                return true;
            }
        }
        delay(1);
    }
    return false;
}

void gps_uart_init() {
    Serial2.setRxBufferSize(1024);

    int cfg_rate = app_config.gps_rate_hz;
    if (cfg_rate < 1 || cfg_rate > 25) {
        cfg_rate = 25;
    }
    GPS_FIX_RATE_HZ = cfg_rate;
    FIX_INTERVAL_US = 1000000 / cfg_rate;
    Serial.printf("[gps] Target rate from config: %d Hz\n", GPS_FIX_RATE_HZ);

    static constexpr long TARGET_BAUD = 115200;
    static constexpr long KNOWN_BAUDS[] = {115200, 38400, 9600};
    static constexpr int NUM_BAUDS = 3;

    long live_baud = 0;
    for (int i = 0; i < NUM_BAUDS; i++) {
        Serial2.end();
        delay(50);
        Serial2.begin(KNOWN_BAUDS[i], SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
        delay(200);
        Serial.printf("[gps] Trying %ld baud... ", KNOWN_BAUDS[i]);
        if (uart_detect_nmea(1500)) {
            live_baud = KNOWN_BAUDS[i];
            Serial.println("OK");
            break;
        }
        Serial.println("no data");
    }

    if (live_baud == 0) {
        Serial.println("[gps] ERROR: no NMEA at any baud rate");
        Serial2.begin(TARGET_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
        return;
    }

    bool need_save = false;

    Serial.println("[gps] Configuring NMEA (GGA+RMC only)...");
    ubx_configure_nmea_sentences();
    delay(50);

    if (live_baud != TARGET_BAUD) {
        Serial.printf("[gps] Switching baud %ld -> %ld...\n", live_baud, TARGET_BAUD);
        ubx_cfg_prt(TARGET_BAUD);
        delay(100);
        Serial2.end();
        delay(50);
        Serial2.begin(TARGET_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
        delay(100);

        if (uart_detect_nmea(2000)) {
            Serial.println("[gps] Baud switch verified");
            need_save = true;
        } else {
            Serial.printf("[gps] WARN: baud switch failed, staying at %ld\n", live_baud);
            Serial2.end();
            delay(50);
            Serial2.begin(live_baud, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
            delay(100);
        }
    } else {
        Serial.println("[gps] Already at 115200");
    }

    Serial.printf("[gps] Setting rate to %d Hz%s\n",
                  GPS_FIX_RATE_HZ,
                  need_save ? " (will persist)" : " (RAM only)");
    ubx_cfg_rate(GPS_FIX_RATE_HZ);
    delay(50);

    // --- Racing / accuracy optimizations (2026-04-21, extended
    // 2026-04-22 with per-write ACK verification) ---------------------
    // These three writes are idempotent — re-applying them on every
    // boot is cheap, and flagging `need_save` here ensures the first
    // boot after a firmware update persists them to the GPS module's
    // own flash so power loss mid-lap never reverts to portable model.
    //
    // Order matters:
    //   1. NAV5 (dynamic model) — takes effect immediately, no restart
    //   2. SBAS              — takes effect immediately, no restart
    //   3. GNSS              — triggers receiver restart (~500-1000 ms),
    //                          so we do this LAST and sleep to let
    //                          NMEA flow return before the cfg_save
    //
    // After each write we now drain the RX buffer through the UBX
    // monitor and wait for the matching UBX-ACK-ACK / UBX-ACK-NAK
    // response, logging `[gps-ubx] ACK|NAK CFG-XXX` lines.  This is
    // the ground truth for whether the config was accepted — before
    // this path, a silently-rejected CFG-GNSS looked identical to a
    // successful one in the boot log, which is exactly the class of
    // bug that hit the 2026-04-21 walking test.
    gps_ubx_monitor_reset();

    Serial.println("[gps] Setting Automotive dynamic model (CFG-NAV5)");
    ubx_cfg_nav5_automotive();
    gps_ubx_monitor_drain_for_ack(0x06, 0x24, 500);
    need_save = true;

    Serial.println("[gps] Enabling SBAS (WAAS/EGNOS/BDSBAS auto-scan)");
    ubx_cfg_sbas_enable();
    gps_ubx_monitor_drain_for_ack(0x06, 0x16, 500);

    Serial.println("[gps] Enabling GPS+SBAS+Galileo+BeiDou+QZSS+GLONASS");
    ubx_cfg_gnss_all();
    // CFG-GNSS specifically: the receiver ACKs BEFORE the restart,
    // then NMEA stops for ~0.5-1 s while the engine re-initialises.
    // Longer timeout here gives us headroom for slower M9N firmware
    // revisions that delay the ACK until after partial restart.
    gps_ubx_monitor_drain_for_ack(0x06, 0x3E, 1500);
    // Extra settle window so any residual NMEA garbage post-restart
    // is drained before we hand bytes back to the NMEA parser.
    delay(800);
    while (Serial2.available() > 0) {
        int c = Serial2.read();
        if (c < 0) break;
        // Feed drained bytes to the UBX monitor too — the receiver
        // sometimes emits extra ACK/NAK during the restart window
        // that a naive drain would have silently swallowed.
        gps_ubx_monitor_feed_byte(static_cast<uint8_t>(c));
    }

    // Ground-truth verification: poll the receiver's actual CFG-GNSS
    // state.  Even if the SET ACKed, certain M9N firmware revisions
    // may silently filter blocks (e.g. unsupported QZSS on certain
    // hardware SKUs, or a signal combination the module doesn't
    // support).  Emits `[gps-verify] active: GPS=Y ...` + a WARN if
    // Galileo or BeiDou didn't stick.
    Serial.println("[gps] Polling CFG-GNSS to verify active constellations");
    ubx_poll_cfg_gnss_and_log(1500);

    if (need_save) {
        Serial.println("[gps] reconfigured: saving to flash");
        ubx_cfg_save();
        gps_ubx_monitor_drain_for_ack(0x06, 0x09, 800);
    } else {
        Serial.println("[gps] already configured (no flash save)");
    }

    Serial.println("[gps] Init complete");
}
