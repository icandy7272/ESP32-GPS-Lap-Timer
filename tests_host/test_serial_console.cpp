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

static void test_gps_stream_rate_command() {
    SerialConsoleCommand cmd = serial_console_parse("gps stream 10");
    assert(cmd.type == SerialConsoleCommandType::GpsStream);
    assert(strcmp(cmd.arg, "10") == 0);
}

static void test_gps_stream_off_command() {
    SerialConsoleCommand cmd = serial_console_parse("gps stream off");
    assert(cmd.type == SerialConsoleCommandType::GpsStream);
    assert(strcmp(cmd.arg, "0") == 0);
}

static void test_gps_stream_rejects_out_of_range_rate() {
    SerialConsoleCommand cmd = serial_console_parse("gps stream 99");
    assert(cmd.type == SerialConsoleCommandType::Invalid);
    assert(strstr(cmd.error, "1-25") != nullptr);
}

// --- "track select <id>" --------------------------------------------
// Live_map's track-picker dropdown emits this when the operator
// chooses a track.  ID must match the track_NNN pattern the firmware
// uses on disk; anything else is path-traversal-shaped garbage.

static void test_track_select_command() {
    SerialConsoleCommand cmd = serial_console_parse("track select track_003");
    assert(cmd.type == SerialConsoleCommandType::TrackSelect);
    assert(strcmp(cmd.arg, "track_003") == 0);
}

static void test_track_select_rejects_missing_id() {
    SerialConsoleCommand cmd = serial_console_parse("track select");
    assert(cmd.type == SerialConsoleCommandType::Invalid);
    assert(strstr(cmd.error, "track_NNN") != nullptr);
}

static void test_track_select_rejects_path_traversal() {
    // Critical: a malformed id mustn't be passed to track_get_by_id
    // / SD reads.  The pattern check rejects anything that isn't
    // exactly track_<digits>.
    SerialConsoleCommand cmd = serial_console_parse("track select ../etc/passwd");
    assert(cmd.type == SerialConsoleCommandType::Invalid);
}

static void test_track_select_rejects_bad_prefix() {
    SerialConsoleCommand cmd = serial_console_parse("track select session_001");
    assert(cmd.type == SerialConsoleCommandType::Invalid);
}

static void test_track_select_rejects_too_many_digits() {
    // 4-digit IDs aren't issued by the firmware's storage layer
    // (max 3 digits per is_track_id_valid).
    SerialConsoleCommand cmd = serial_console_parse("track select track_1234");
    assert(cmd.type == SerialConsoleCommandType::Invalid);
}

static void test_track_autodetect_command() {
    SerialConsoleCommand cmd = serial_console_parse("track autodetect");
    assert(cmd.type == SerialConsoleCommandType::TrackAutodetect);
}

// --- "tracks list" -------------------------------------------------
// Catalog dump for the dropdown.  The plural prefix matters — must
// not be eaten by the singular `track` branch.

static void test_tracks_list_command() {
    SerialConsoleCommand cmd = serial_console_parse("tracks list");
    assert(cmd.type == SerialConsoleCommandType::TracksList);
}

static void test_tracks_unknown_subcommand_rejected() {
    SerialConsoleCommand cmd = serial_console_parse("tracks foo");
    assert(cmd.type == SerialConsoleCommandType::Invalid);
    assert(strstr(cmd.error, "list") != nullptr);
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
    test_gps_stream_rate_command();
    test_gps_stream_off_command();
    test_gps_stream_rejects_out_of_range_rate();
    test_track_select_command();
    test_track_select_rejects_missing_id();
    test_track_select_rejects_path_traversal();
    test_track_select_rejects_bad_prefix();
    test_track_select_rejects_too_many_digits();
    test_track_autodetect_command();
    test_tracks_list_command();
    test_tracks_unknown_subcommand_rejected();
    return 0;
}
