#include "serial_console.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

void init_command(SerialConsoleCommand* cmd) {
    cmd->type = SerialConsoleCommandType::Invalid;
    cmd->arg[0] = '\0';
    cmd->error[0] = '\0';
}

void set_error(SerialConsoleCommand* cmd, const char* message) {
    if (!message) {
        cmd->error[0] = '\0';
        return;
    }
    snprintf(cmd->error, sizeof(cmd->error), "%s", message);
}

void trim_copy(char* out, size_t out_len, const char* src) {
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!src) {
        return;
    }

    const char* start = src;
    while (*start && isspace(static_cast<unsigned char>(*start))) {
        start++;
    }

    const char* end = start + strlen(start);
    while (end > start && isspace(static_cast<unsigned char>(end[-1]))) {
        end--;
    }

    size_t len = static_cast<size_t>(end - start);
    if (len >= out_len) {
        len = out_len - 1;
    }
    memcpy(out, start, len);
    out[len] = '\0';
}

bool starts_with(const char* value, const char* prefix) {
    return strncmp(value, prefix, strlen(prefix)) == 0;
}

bool is_allowed_directory(const char* arg) {
    return strcmp(arg, "tracks") == 0 || strcmp(arg, "sessions") == 0;
}

bool has_path_traversal(const char* path) {
    return strstr(path, "..") != nullptr;
}

bool is_allowed_cat_path(const char* path) {
    if (!path || path[0] == '\0') {
        return false;
    }
    if (path[0] == '/') {
        return false;
    }
    if (has_path_traversal(path)) {
        return false;
    }

    const char* filename = nullptr;
    if (starts_with(path, "tracks/")) {
        filename = path + strlen("tracks/");
    } else if (starts_with(path, "sessions/")) {
        filename = path + strlen("sessions/");
    } else {
        return false;
    }

    if (!filename || filename[0] == '\0') {
        return false;
    }
    if (strchr(filename, '/') != nullptr) {
        return false;
    }
    return true;
}

// Validate a draft track name the same way the web-UI does on the
// server (api_tracks.cpp:is_ascii_safe): printable ASCII, no
// filesystem-reserved characters, not empty.  Keeps the serial entry
// point from writing track files that would later be rejected by the
// normal POST /api/tracks flow.
bool is_track_name_valid(const char* name) {
    if (!name || name[0] == '\0') {
        return false;
    }
    for (int i = 0; name[i]; i++) {
        char c = name[i];
        if (c < 0x20 || c > 0x7E) return false;
        if (c == '"' || c == '\\' || c == '/' || c == ':' ||
            c == '*' || c == '?' || c == '<' || c == '>' || c == '|') {
            return false;
        }
    }
    return true;
}

// Track IDs are the filename stem pattern emitted by the firmware's
// own storage layer — `track_NNN` where NNN is 1-3 digits.  Tight
// pattern on purpose: live_map must not be able to inject a path
// traversal or a malformed id that would confuse track_get_by_id
// or subsequent SD reads.  Matches `^track_\d{1,3}$`.
bool is_track_id_valid(const char* id) {
    if (!id || id[0] == '\0') return false;
    const char* prefix = "track_";
    for (int i = 0; prefix[i] != '\0'; i++) {
        if (id[i] != prefix[i]) return false;
    }
    const char* digits = id + 6;  // strlen("track_")
    int ndigits = 0;
    while (digits[ndigits] != '\0') {
        if (!isdigit(static_cast<unsigned char>(digits[ndigits]))) return false;
        ndigits++;
        if (ndigits > 3) return false;
    }
    return ndigits >= 1;
}

const char* skip_spaces(const char* p) {
    while (*p && isspace(static_cast<unsigned char>(*p))) {
        p++;
    }
    return p;
}

}  // namespace

SerialConsoleCommand serial_console_parse(const char* line) {
    SerialConsoleCommand cmd = {};
    init_command(&cmd);

    char trimmed[160];
    trim_copy(trimmed, sizeof(trimmed), line);
    if (trimmed[0] == '\0') {
        set_error(&cmd, "empty command");
        return cmd;
    }

    if (strcmp(trimmed, "help") == 0) {
        cmd.type = SerialConsoleCommandType::Help;
        return cmd;
    }

    if (starts_with(trimmed, "ls")) {
        const char* arg = skip_spaces(trimmed + 2);
        if (!is_allowed_directory(arg)) {
            set_error(&cmd, "ls expects tracks or sessions");
            return cmd;
        }
        cmd.type = SerialConsoleCommandType::ListDirectory;
        snprintf(cmd.arg, sizeof(cmd.arg), "%s", arg);
        return cmd;
    }

    if (starts_with(trimmed, "cat")) {
        const char* arg = skip_spaces(trimmed + 3);
        if (!is_allowed_cat_path(arg)) {
            set_error(&cmd, "cat expects tracks/<file> or sessions/<file>");
            return cmd;
        }
        cmd.type = SerialConsoleCommandType::CatFile;
        snprintf(cmd.arg, sizeof(cmd.arg), "%s", arg);
        return cmd;
    }

    // "tracks list" — catalog listing for the live_map track picker.
    // Checked BEFORE the singular "track" branch because both share the
    // same prefix; an unconditional starts_with("track") would swallow
    // "tracks list" as "track s list" (invalid) before we can route it.
    if (starts_with(trimmed, "tracks")) {
        const char* tail = skip_spaces(trimmed + strlen("tracks"));
        if (strcmp(tail, "list") == 0) {
            cmd.type = SerialConsoleCommandType::TracksList;
            return cmd;
        }
        set_error(&cmd, "tracks expects list");
        return cmd;
    }

    // "track save", "track cancel", "track status", "track draft <name>",
    // "track select <id>", "track autodetect"
    if (starts_with(trimmed, "track")) {
        const char* tail = skip_spaces(trimmed + strlen("track"));
        if (strcmp(tail, "save") == 0) {
            cmd.type = SerialConsoleCommandType::TrackSave;
            return cmd;
        }
        if (strcmp(tail, "cancel") == 0) {
            cmd.type = SerialConsoleCommandType::TrackCancel;
            return cmd;
        }
        if (strcmp(tail, "status") == 0) {
            cmd.type = SerialConsoleCommandType::TrackStatus;
            return cmd;
        }
        if (strcmp(tail, "autodetect") == 0) {
            cmd.type = SerialConsoleCommandType::TrackAutodetect;
            return cmd;
        }
        if (starts_with(tail, "select")) {
            const char* id = skip_spaces(tail + strlen("select"));
            if (!is_track_id_valid(id)) {
                set_error(&cmd, "track select expects track_NNN");
                return cmd;
            }
            if (strlen(id) >= sizeof(cmd.arg)) {
                set_error(&cmd, "track id too long");
                return cmd;
            }
            cmd.type = SerialConsoleCommandType::TrackSelect;
            snprintf(cmd.arg, sizeof(cmd.arg), "%s", id);
            return cmd;
        }
        if (starts_with(tail, "draft")) {
            const char* name = skip_spaces(tail + strlen("draft"));
            if (!is_track_name_valid(name)) {
                set_error(&cmd, "track draft expects a valid name");
                return cmd;
            }
            if (strlen(name) >= sizeof(cmd.arg)) {
                set_error(&cmd, "track name too long");
                return cmd;
            }
            cmd.type = SerialConsoleCommandType::TrackDraftStart;
            snprintf(cmd.arg, sizeof(cmd.arg), "%s", name);
            return cmd;
        }
        set_error(&cmd, "track expects draft|save|cancel|status|select|autodetect");
        return cmd;
    }

    if (starts_with(trimmed, "mark")) {
        const char* tail = skip_spaces(trimmed + strlen("mark"));
        if (strcmp(tail, "p1") == 0) {
            cmd.type = SerialConsoleCommandType::TrackMarkP1;
            return cmd;
        }
        if (strcmp(tail, "p2") == 0) {
            cmd.type = SerialConsoleCommandType::TrackMarkP2;
            return cmd;
        }
        set_error(&cmd, "mark expects p1 or p2");
        return cmd;
    }

    // "gps stream 10" / "gps stream off" — enables a compact
    // high-rate USB serial position stream for tools/live_map.py
    // without changing the normal 1 Hz [gps] diagnostic line.
    if (starts_with(trimmed, "gps")) {
        const char* tail = skip_spaces(trimmed + strlen("gps"));
        if (starts_with(tail, "stream")) {
            const char* rate_arg = skip_spaces(tail + strlen("stream"));
            if (strcmp(rate_arg, "off") == 0 || strcmp(rate_arg, "0") == 0) {
                cmd.type = SerialConsoleCommandType::GpsStream;
                snprintf(cmd.arg, sizeof(cmd.arg), "0");
                return cmd;
            }
            if (rate_arg[0] == '\0') {
                set_error(&cmd, "gps stream expects 1-25 or off");
                return cmd;
            }
            char* end = nullptr;
            long rate = strtol(rate_arg, &end, 10);
            if (end == rate_arg || *skip_spaces(end) != '\0'
                || rate < 1 || rate > 25) {
                set_error(&cmd, "gps stream expects 1-25 or off");
                return cmd;
            }
            cmd.type = SerialConsoleCommandType::GpsStream;
            snprintf(cmd.arg, sizeof(cmd.arg), "%ld", rate);
            return cmd;
        }
        set_error(&cmd, "gps expects stream");
        return cmd;
    }

    // "recording start" / "recording stop" — mirrors /api/recording.
    // Lets live_map drive session start/stop over USB without needing
    // the laptop on the board's AP (the laptop is normally on the
    // phone hotspot for satellite map tiles).
    if (starts_with(trimmed, "recording")) {
        const char* tail = skip_spaces(trimmed + strlen("recording"));
        if (strcmp(tail, "start") == 0) {
            cmd.type = SerialConsoleCommandType::RecordingStart;
            return cmd;
        }
        if (strcmp(tail, "stop") == 0) {
            cmd.type = SerialConsoleCommandType::RecordingStop;
            return cmd;
        }
        set_error(&cmd, "recording expects start or stop");
        return cmd;
    }

    set_error(&cmd, "unknown command");
    return cmd;
}
