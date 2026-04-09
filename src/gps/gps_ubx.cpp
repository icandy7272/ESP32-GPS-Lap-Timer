#include "gps/gps_internal.h"

#include "config.h"
#include "pins.h"

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

    ubx_cfg_msg(CLS, 0x00, 1);
    ubx_cfg_msg(CLS, 0x04, 1);
    ubx_cfg_msg(CLS, 0x03, 0);
    ubx_cfg_msg(CLS, 0x02, 0);
    ubx_cfg_msg(CLS, 0x01, 0);
    ubx_cfg_msg(CLS, 0x05, 0);
    ubx_cfg_msg(CLS, 0x08, 0);
}

static void ubx_cfg_save() {
    uint8_t payload[13] = {};
    payload[4] = 0xFF;
    payload[5] = 0xFF;
    payload[12] = 0x07;
    ubx_send(0x06, 0x09, payload, sizeof(payload));
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

    if (need_save) {
        Serial.println("[gps] reconfigured: saving to flash");
        ubx_cfg_save();
        delay(100);
    } else {
        Serial.println("[gps] already configured (no flash save)");
    }

    Serial.println("[gps] Init complete");
}
