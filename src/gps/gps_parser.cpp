#include "gps/gps_internal.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

enum class NmeaType {
    GGA,
    RMC,
    UNKNOWN
};

static bool nmea_checksum_valid(const char* sentence, int len) {
    if (len < 4 || sentence[0] != '$') {
        return false;
    }

    uint8_t computed = 0;
    int star_pos = -1;

    for (int i = 1; i < len; i++) {
        if (sentence[i] == '*') {
            star_pos = i;
            break;
        }
        computed ^= static_cast<uint8_t>(sentence[i]);
    }

    if (star_pos < 0 || star_pos + 2 >= len) {
        return false;
    }

    char hex[3] = { sentence[star_pos + 1], sentence[star_pos + 2], '\0' };
    uint8_t expected = static_cast<uint8_t>(strtoul(hex, nullptr, 16));
    return computed == expected;
}

static double nmea_coord_to_deg(const char* field, int deg_digits) {
    if (field[0] == '\0') {
        return 0.0;
    }

    char deg_str[4] = {};
    for (int i = 0; i < deg_digits && i < 3; i++) {
        deg_str[i] = field[i];
    }

    double degrees = atof(deg_str);
    double minutes = atof(field + deg_digits);
    return degrees + (minutes / 60.0);
}

static double apply_hemisphere(double value, char dir) {
    if (dir == 'S' || dir == 'W') {
        return -value;
    }
    return value;
}

static int nmea_split_fields(char* body, char* fields[], int max_fields) {
    int count = 0;
    char* ptr = body;

    while (ptr != nullptr && count < max_fields) {
        fields[count++] = ptr;
        char* comma = strchr(ptr, ',');
        if (comma != nullptr) {
            *comma = '\0';
            ptr = comma + 1;
        } else {
            break;
        }
    }

    return count;
}

static GgaData parse_gga(char* body) {
    GgaData result = {};
    char* fields[MAX_FIELDS] = {};
    int count = nmea_split_fields(body, fields, MAX_FIELDS);

    if (count < 10) {
        return result;
    }

    result.fix_quality = atoi(fields[5]);
    if (result.fix_quality == 0) {
        return result;
    }

    result.lat_deg    = apply_hemisphere(nmea_coord_to_deg(fields[1], 2), fields[2][0]);
    result.lon_deg    = apply_hemisphere(nmea_coord_to_deg(fields[3], 3), fields[4][0]);
    result.satellites = atoi(fields[6]);
    result.height_m   = static_cast<float>(atof(fields[8]));
    result.valid      = true;
    return result;
}

static void sync_rtc_from_rmc(const char* time_str, const char* date_str) {
    if (s_rtc_synced) {
        return;
    }

    if (strlen(time_str) < 6 || strlen(date_str) < 6) {
        return;
    }

    int hour   = (time_str[0] - '0') * 10 + (time_str[1] - '0');
    int minute = (time_str[2] - '0') * 10 + (time_str[3] - '0');
    int second = (time_str[4] - '0') * 10 + (time_str[5] - '0');
    int day    = (date_str[0] - '0') * 10 + (date_str[1] - '0');
    int month  = (date_str[2] - '0') * 10 + (date_str[3] - '0');
    int year   = (date_str[4] - '0') * 10 + (date_str[5] - '0') + 2000;

    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = minute;
    t.tm_sec  = second;

    time_t epoch = mktime(&t);
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, nullptr);

    s_rtc_synced = true;
    Serial.println("[gps] RTC synced from GPS");
}

static RmcData parse_rmc(char* body) {
    RmcData result = {};
    char* fields[MAX_FIELDS] = {};
    int count = nmea_split_fields(body, fields, MAX_FIELDS);

    if (count < 9) {
        return result;
    }

    if (fields[1][0] != 'A') {
        return result;
    }

    double speed_knots = atof(fields[6]);
    result.speed_kmh   = static_cast<float>(speed_knots * 1.852);
    result.heading_deg = (fields[7][0] != '\0') ? static_cast<float>(atof(fields[7])) : 0.0f;
    result.valid       = true;

    sync_rtc_from_rmc(fields[0], fields[8]);
    return result;
}

static NmeaType detect_sentence_type(const char* sentence) {
    if (strncmp(sentence + 3, "GGA", 3) == 0) {
        return NmeaType::GGA;
    }
    if (strncmp(sentence + 3, "RMC", 3) == 0) {
        return NmeaType::RMC;
    }
    return NmeaType::UNKNOWN;
}

static bool has_valid_prefix(const char* sentence) {
    if (sentence[0] != '$') {
        return false;
    }

    bool gn = (sentence[1] == 'G' && sentence[2] == 'N');
    bool gp = (sentence[1] == 'G' && sentence[2] == 'P');
    return gn || gp;
}

static void process_sentence(char* sentence, int len) {
    if (!nmea_checksum_valid(sentence, len)) {
        return;
    }
    if (!has_valid_prefix(sentence)) {
        return;
    }

    NmeaType type = detect_sentence_type(sentence);
    if (type == NmeaType::UNKNOWN) {
        return;
    }

    char* body = strchr(sentence + 1, ',');
    if (body == nullptr) {
        return;
    }
    body++;

    char* star = strchr(body, '*');
    if (star != nullptr) {
        *star = '\0';
    }

    if (type == NmeaType::GGA) {
        s_gga = parse_gga(body);
    } else {
        s_rmc = parse_rmc(body);
        gps_send_fix_if_ready();
    }
}

void gps_feed_char(char c) {
    if (c == '$') {
        s_nmea_buf[0] = '$';
        s_nmea_len = 1;
        s_nmea_receiving = true;
        return;
    }

    if (!s_nmea_receiving) {
        return;
    }

    if (c == '\r' || c == '\n') {
        if (s_nmea_len > 6) {
            s_nmea_buf[s_nmea_len] = '\0';
            process_sentence(s_nmea_buf, s_nmea_len);
        }
        s_nmea_receiving = false;
        s_nmea_len = 0;
        return;
    }

    if (s_nmea_len < NMEA_MAX_LEN) {
        s_nmea_buf[s_nmea_len++] = c;
    } else {
        s_nmea_receiving = false;
        s_nmea_len = 0;
    }
}
