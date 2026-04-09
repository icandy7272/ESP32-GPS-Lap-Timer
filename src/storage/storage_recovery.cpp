#include "storage_internal.h"

#include "../pins.h"

#include <Arduino.h>
#include <string.h>

namespace storage_internal {

static bool parse_creation_line(const char* line,
                                int* day, int* mon, int* year,
                                int* hour, int* min, int* sec) {
    return (sscanf(line,
                   "File created on %d/%d/%d at %d:%d:%d",
                   day, mon, year, hour, min, sec) == 6);
}

bool ensure_directories() {
    xSemaphoreTake(spi_mutex, portMAX_DELAY);

    if (!sd.exists(SESSIONS_DIR)) {
        if (!sd.mkdir(SESSIONS_DIR)) {
            xSemaphoreGive(spi_mutex);
            Serial.println("[storage] failed to create sessions/");
            return false;
        }
    }
    if (!sd.exists(TRACKS_DIR)) {
        if (!sd.mkdir(TRACKS_DIR)) {
            xSemaphoreGive(spi_mutex);
            Serial.println("[storage] failed to create tracks/");
            return false;
        }
    }

    xSemaphoreGive(spi_mutex);
    return true;
}

bool recover_tmp_file() {
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    bool tmp_exists = sd.exists(TMP_FILENAME);
    xSemaphoreGive(spi_mutex);

    if (!tmp_exists) {
        return false;
    }

    Serial.println("[storage] found .tmp file — running recovery");

    char first_line[80] = {0};
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    FsFile tmp;
    if (tmp.open(TMP_FILENAME, O_RDONLY)) {
        tmp.fgets(first_line, sizeof(first_line));
        tmp.close();
    }
    xSemaphoreGive(spi_mutex);

    char recovery_name[PATH_BUF_LEN];
    int day;
    int mon;
    int year;
    int hour;
    int min;
    int sec;
    bool parsed = parse_creation_line(first_line, &day, &mon, &year,
                                      &hour, &min, &sec);
    if (parsed) {
        snprintf(recovery_name, sizeof(recovery_name),
                 "%s/%04d%02d%02d_UNKNOWN_%02d%02d%02d_recovered.vbo",
                 SESSIONS_DIR, year, mon, day, hour, min, sec);
    } else {
        snprintf(recovery_name, sizeof(recovery_name),
                 "%s/00000000_UNKNOWN_000000_recovered.vbo",
                 SESSIONS_DIR);
    }

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    bool final_exists = sd.exists(recovery_name);
    xSemaphoreGive(spi_mutex);

    if (final_exists) {
        xSemaphoreTake(spi_mutex, portMAX_DELAY);
        sd.remove(TMP_FILENAME);
        xSemaphoreGive(spi_mutex);
        Serial.println("[storage] deleted orphan .tmp (final file exists)");
    } else {
        xSemaphoreTake(spi_mutex, portMAX_DELAY);
        sd.rename(TMP_FILENAME, recovery_name);
        xSemaphoreGive(spi_mutex);

        sync_directory(SESSIONS_DIR);
        storage_recovered = true;
        Serial.printf("[storage] recovered session: %s\n", recovery_name);
    }

    return true;
}

}  // namespace storage_internal

bool storage_init() {
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    bool ok = sd.begin(SdSpiConfig(PIN_SD_CS, SHARED_SPI,
                                   SD_SCK_MHZ(storage_internal::SD_SPI_MHZ)));
    xSemaphoreGive(spi_mutex);

    if (!ok) {
        Serial.println("[storage] SD card init failed");
        return false;
    }
    Serial.println("[storage] SD card mounted");

    if (!storage_internal::ensure_directories()) {
        return false;
    }

    storage_internal::recover_tmp_file();
    return true;
}
