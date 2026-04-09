#pragma once

#include "track.h"

#include <SdFat.h>
#include <freertos/semphr.h>

static constexpr int PATH_BUF_LEN = 128;
static constexpr int JSON_BUF_SIZE = 2048;
static constexpr double AUTO_DETECT_MAX_M = 5000.0;
static constexpr const char* TRACKS_DIR = "tracks";

extern SemaphoreHandle_t spi_mutex;
extern SdFat sd;

extern TrackDefinition s_tracks[MAX_TRACKS];
extern int s_track_count;

bool track_parse_track_json(const char* json, TrackDefinition* out);
bool track_format_track_json(const TrackDefinition* track,
                             char* buf, int buf_len);
