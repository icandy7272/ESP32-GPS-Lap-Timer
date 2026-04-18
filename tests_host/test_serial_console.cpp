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

// ============================================================
// Track-marking commands used by tools/live_map.py
// ============================================================

static void test_track_save_command() {
    SerialConsoleCommand cmd = serial_console_parse("track save");
    assert(cmd.type == SerialConsoleCommandType::TrackSave);
    assert(cmd.arg[0] == '\0');
}

static void test_track_cancel_command() {
    SerialConsoleCommand cmd = serial_console_parse("track cancel");
    assert(cmd.type == SerialConsoleCommandType::TrackCancel);
}

static void test_track_status_command() {
    SerialConsoleCommand cmd = serial_console_parse("track status");
    assert(cmd.type == SerialConsoleCommandType::TrackStatus);
}

static void test_track_draft_with_name() {
    SerialConsoleCommand cmd = serial_console_parse("track draft home_west");
    assert(cmd.type == SerialConsoleCommandType::TrackDraftStart);
    assert(strcmp(cmd.arg, "home_west") == 0);
}

static void test_track_draft_trims_extra_spaces() {
    SerialConsoleCommand cmd =
        serial_console_parse("  track   draft   some name  ");
    assert(cmd.type == SerialConsoleCommandType::TrackDraftStart);
    // Inner spaces are preserved after the leading "draft" keyword; the
    // "skip whitespace" step only strips leading whitespace of the
    // argument.
    assert(strcmp(cmd.arg, "some name") == 0);
}

static void test_track_draft_rejects_empty_name() {
    SerialConsoleCommand cmd = serial_console_parse("track draft");
    assert(cmd.type == SerialConsoleCommandType::Invalid);
    assert(strstr(cmd.error, "valid name") != nullptr);
}

static void test_track_draft_rejects_bad_chars() {
    SerialConsoleCommand cmd = serial_console_parse("track draft hack/../etc");
    assert(cmd.type == SerialConsoleCommandType::Invalid);
}

static void test_track_draft_rejects_quote() {
    SerialConsoleCommand cmd = serial_console_parse("track draft bad\"name");
    assert(cmd.type == SerialConsoleCommandType::Invalid);
}

static void test_track_unknown_subcommand() {
    SerialConsoleCommand cmd = serial_console_parse("track restart");
    assert(cmd.type == SerialConsoleCommandType::Invalid);
    assert(strstr(cmd.error, "draft") != nullptr);
}

static void test_mark_p1_command() {
    SerialConsoleCommand cmd = serial_console_parse("mark p1");
    assert(cmd.type == SerialConsoleCommandType::TrackMarkP1);
}

static void test_mark_p2_command() {
    SerialConsoleCommand cmd = serial_console_parse("mark p2");
    assert(cmd.type == SerialConsoleCommandType::TrackMarkP2);
}

static void test_mark_rejects_bad_subarg() {
    SerialConsoleCommand cmd = serial_console_parse("mark p3");
    assert(cmd.type == SerialConsoleCommandType::Invalid);
    assert(strstr(cmd.error, "p1") != nullptr);
}

int main() {
    test_help_command_parses();
    test_ls_command_trims_whitespace();
    test_cat_tracks_file_parses();
    test_invalid_path_is_rejected();
    test_invalid_directory_is_rejected();

    test_track_save_command();
    test_track_cancel_command();
    test_track_status_command();
    test_track_draft_with_name();
    test_track_draft_trims_extra_spaces();
    test_track_draft_rejects_empty_name();
    test_track_draft_rejects_bad_chars();
    test_track_draft_rejects_quote();
    test_track_unknown_subcommand();
    test_mark_p1_command();
    test_mark_p2_command();
    test_mark_rejects_bad_subarg();
    return 0;
}
