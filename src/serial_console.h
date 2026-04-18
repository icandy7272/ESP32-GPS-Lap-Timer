#pragma once

#include <stddef.h>

enum class SerialConsoleCommandType {
    Invalid,
    Help,
    ListDirectory,
    CatFile,
    // Track-marking commands — used by tools/live_map.py so the
    // operator can mark a new start/finish segment over USB without
    // switching the laptop's Wi-Fi off the phone hotspot to reach the
    // ESP32 AP for the normal web track creation UI.
    TrackDraftStart,   // "track draft <name>"
    TrackMarkP1,       // "mark p1"
    TrackMarkP2,       // "mark p2"
    TrackSave,         // "track save"
    TrackCancel,       // "track cancel"
    TrackStatus,       // "track status"
};

struct SerialConsoleCommand {
    SerialConsoleCommandType type;
    char arg[128];
    char error[96];
};

SerialConsoleCommand serial_console_parse(const char* line);
