#include "serial_console.h"

#include <ctype.h>
#include <stdio.h>
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
        const char* arg = trimmed + 2;
        while (*arg && isspace(static_cast<unsigned char>(*arg))) {
            arg++;
        }
        if (!is_allowed_directory(arg)) {
            set_error(&cmd, "ls expects tracks or sessions");
            return cmd;
        }
        cmd.type = SerialConsoleCommandType::ListDirectory;
        snprintf(cmd.arg, sizeof(cmd.arg), "%s", arg);
        return cmd;
    }

    if (starts_with(trimmed, "cat")) {
        const char* arg = trimmed + 3;
        while (*arg && isspace(static_cast<unsigned char>(*arg))) {
            arg++;
        }
        if (!is_allowed_cat_path(arg)) {
            set_error(&cmd, "cat expects tracks/<file> or sessions/<file>");
            return cmd;
        }
        cmd.type = SerialConsoleCommandType::CatFile;
        snprintf(cmd.arg, sizeof(cmd.arg), "%s", arg);
        return cmd;
    }

    set_error(&cmd, "unknown command");
    return cmd;
}
