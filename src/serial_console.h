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
    // Track selection — lets live_map (and any other serial client)
    // switch the active track without dropping to WiFi.  Mirrors
    // the /api/tracks/select HTTP endpoint behaviour: refuses if
    // currently recording.
    TrackSelect,       // "track select <id>"
    TrackAutodetect,   // "track autodetect"
    // Catalog listing for the live_map track picker.  Emits one
    // "[tracks-list] <id> <name>" line per track + a
    // "[tracks-list] end" terminator so the client knows when to
    // stop collecting.  Cheaper than `ls tracks` + iterated `cat`.
    TracksList,        // "tracks list"
    // Recording control — lets live_map (and any other serial client)
    // toggle session recording without needing WiFi access.  Matches
    // the /api/recording HTTP endpoint behaviour.
    RecordingStart,    // "recording start"
    RecordingStop,     // "recording stop"
    // High-rate live-map GPS stream. Keeps normal [gps] diagnostics at
    // 1 Hz while allowing tools/live_map.py to request a lightweight
    // position feed closer to the configured GPS fix rate.
    GpsStream,         // "gps stream <1-25|off>"
};

struct SerialConsoleCommand {
    SerialConsoleCommandType type;
    char arg[128];
    char error[96];
};

SerialConsoleCommand serial_console_parse(const char* line);
