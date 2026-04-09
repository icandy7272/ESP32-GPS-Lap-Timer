#include "storage_naming.h"

#include <stdio.h>
#include <string.h>

void storage_naming_format_final_path(const char* session_start_ts,
                                      const char* track_name,
                                      int seq,
                                      char* path,
                                      size_t path_len) {
    char date_part[9] = {0};
    char time_part[7] = {0};

    if (session_start_ts != nullptr) {
        strncpy(date_part, session_start_ts, sizeof(date_part) - 1);
        if (strlen(session_start_ts) >= 15) {
            strncpy(time_part, session_start_ts + 9, sizeof(time_part) - 1);
        }
    }

    snprintf(path, path_len, "sessions/%s_%s_%s_%03d.vbo",
             date_part, track_name != nullptr ? track_name : "",
             time_part, seq);
}
