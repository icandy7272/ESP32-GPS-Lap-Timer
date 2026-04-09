#include "track_internal.h"

#include <stdio.h>

bool track_format_track_json(const TrackDefinition* track, char* buf, int buf_len) {
    int pos = 0;

    pos += snprintf(buf + pos, buf_len - pos,
                    "{\n"
                    "  \"id\": \"%s\",\n"
                    "  \"name\": \"%s\",\n"
                    "  \"center_lat\": %.7f,\n"
                    "  \"center_lon\": %.7f,\n"
                    "  \"approx_length_m\": %.0f,\n",
                    track->id,
                    track->name,
                    track->center_lat_deg,
                    track->center_lon_deg,
                    (double)track->approx_length_m);

    pos += snprintf(buf + pos, buf_len - pos,
                    "  \"start_finish\": {\n"
                    "    \"lat1\": %.7f,\n"
                    "    \"lon1\": %.7f,\n"
                    "    \"lat2\": %.7f,\n"
                    "    \"lon2\": %.7f,\n"
                    "    \"heading\": %.1f\n"
                    "  }",
                    track->start_finish.lat1_deg,
                    track->start_finish.lon1_deg,
                    track->start_finish.lat2_deg,
                    track->start_finish.lon2_deg,
                    (double)track->start_finish.valid_heading_deg);

    int sector_lines = track->sector_count - 1;
    if (sector_lines > 0) {
        pos += snprintf(buf + pos, buf_len - pos, ",\n  \"sectors\": [\n");

        for (int i = 0; i < sector_lines && i < MAX_SECTORS - 1; i++) {
            if (i > 0) {
                pos += snprintf(buf + pos, buf_len - pos, ",\n");
            }
            const DetectionLine* s = &track->sectors[i];
            pos += snprintf(buf + pos, buf_len - pos,
                            "    {\n"
                            "      \"lat1\": %.7f,\n"
                            "      \"lon1\": %.7f,\n"
                            "      \"lat2\": %.7f,\n"
                            "      \"lon2\": %.7f,\n"
                            "      \"heading\": %.1f\n"
                            "    }",
                            s->lat1_deg, s->lon1_deg,
                            s->lat2_deg, s->lon2_deg,
                            (double)s->valid_heading_deg);
        }

        pos += snprintf(buf + pos, buf_len - pos, "\n  ]");
    } else {
        pos += snprintf(buf + pos, buf_len - pos, ",\n  \"sectors\": []");
    }

    pos += snprintf(buf + pos, buf_len - pos, "\n}\n");
    return (pos < buf_len);
}
