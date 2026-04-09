#include "storage_internal.h"

#include "../storage_naming.h"

#include <Arduino.h>
#include <string.h>

namespace storage_internal {

bool has_vbo_extension(const char* name) {
    size_t len = strlen(name);
    if (len < 5) {
        return false;
    }
    return (strcasecmp(name + len - 4, ".vbo") == 0);
}

void sync_directory(const char* dir_path) {
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    FsFile dir;
    if (dir.open(dir_path, O_RDONLY)) {
        dir.sync();
        dir.close();
    }
    xSemaphoreGive(spi_mutex);
}

void build_final_path(char* path, int path_len) {
    int seq = 1;
    char probe[PATH_BUF_LEN];

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    for (; seq <= 999; seq++) {
        storage_naming_format_final_path(s_session_start_ts,
                                         s_active_track_name,
                                         seq,
                                         probe,
                                         sizeof(probe));
        if (!sd.exists(probe)) {
            break;
        }
    }
    xSemaphoreGive(spi_mutex);

    storage_naming_format_final_path(s_session_start_ts,
                                     s_active_track_name,
                                     seq,
                                     path,
                                     path_len);
}

}  // namespace storage_internal

int storage_list_sessions(char names[][64], int max) {
    int count = 0;

    xSemaphoreTake(spi_mutex, portMAX_DELAY);

    FsFile dir;
    if (!dir.open(storage_internal::SESSIONS_DIR, O_RDONLY)) {
        xSemaphoreGive(spi_mutex);
        return 0;
    }

    FsFile entry;
    while (count < max && entry.openNext(&dir, O_RDONLY)) {
        if (!entry.isDir()) {
            char fname[64];
            entry.getName(fname, sizeof(fname));
            if (storage_internal::has_vbo_extension(fname)) {
                strncpy(names[count], fname, 63);
                names[count][63] = '\0';
                count++;
            }
        }
        entry.close();
    }
    dir.close();

    xSemaphoreGive(spi_mutex);
    return count;
}

bool storage_get_session_path(const char* name, char* path, int path_len) {
    snprintf(path, path_len, "%s/%s", storage_internal::SESSIONS_DIR, name);

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    bool exists = sd.exists(path);
    xSemaphoreGive(spi_mutex);

    return exists;
}
