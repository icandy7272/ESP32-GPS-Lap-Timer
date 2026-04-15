#pragma once

#include <stddef.h>

enum class SerialConsoleCommandType {
    Invalid,
    Help,
    ListDirectory,
    CatFile,
};

struct SerialConsoleCommand {
    SerialConsoleCommandType type;
    char arg[128];
    char error[96];
};

SerialConsoleCommand serial_console_parse(const char* line);
