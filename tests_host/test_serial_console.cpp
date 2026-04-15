#include "serial_console.h"

#include <assert.h>
#include <string.h>

static void test_help_command_parses() {
    SerialConsoleCommand cmd = serial_console_parse("help");
    assert(cmd.type == SerialConsoleCommandType::Help);
    assert(cmd.arg[0] == '\0');
}

static void test_ls_command_trims_whitespace() {
    SerialConsoleCommand cmd = serial_console_parse("  ls sessions  ");
    assert(cmd.type == SerialConsoleCommandType::ListDirectory);
    assert(strcmp(cmd.arg, "sessions") == 0);
}

static void test_cat_tracks_file_parses() {
    SerialConsoleCommand cmd = serial_console_parse("cat tracks/track_001.json");
    assert(cmd.type == SerialConsoleCommandType::CatFile);
    assert(strcmp(cmd.arg, "tracks/track_001.json") == 0);
}

static void test_invalid_path_is_rejected() {
    SerialConsoleCommand cmd = serial_console_parse("cat ../secret");
    assert(cmd.type == SerialConsoleCommandType::Invalid);
    assert(strstr(cmd.error, "cat") != nullptr);
}

static void test_invalid_directory_is_rejected() {
    SerialConsoleCommand cmd = serial_console_parse("ls logs");
    assert(cmd.type == SerialConsoleCommandType::Invalid);
}

int main() {
    test_help_command_parses();
    test_ls_command_trims_whitespace();
    test_cat_tracks_file_parses();
    test_invalid_path_is_rejected();
    test_invalid_directory_is_rejected();
    return 0;
}
