#include <assert.h>
#include <string.h>

#include "boot_presenter.h"
#include "boot_status.h"

static void copy_detail(char* dest, size_t dest_size, const char* detail) {
    if (dest_size == 0) {
        return;
    }
    if (detail == nullptr) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, detail, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static void set_status(BootStatus& status,
                       BootStage stage,
                       BootState state,
                       const char* detail) {
    status.stage = stage;
    status.state = state;
    status.progress_segment = 0xFF;
    copy_detail(status.display_detail, sizeof(status.display_detail), detail);
}

int main() {
    BootStatus status{};

    set_status(status, BootStage::STORAGE, BootState::WARN, "session recovered");
    BootView storage_view = boot_present(status);
    assert(strcmp(storage_view.stage_label, "STORAGE WARN") == 0);
    assert(strcmp(storage_view.detail_line, "session recovered") == 0);
    assert(storage_view.completed_segments == 2);
    assert(storage_view.active_segment == 2);
    assert(!storage_view.fatal);

    set_status(status, BootStage::GPS, BootState::START, "waiting for fix");
    BootView fix_wait_view = boot_present(status);
    assert(strcmp(fix_wait_view.stage_label, "GPS") == 0);
    assert(strcmp(fix_wait_view.detail_line, "waiting for fix") == 0);
    assert(fix_wait_view.completed_segments == 3);
    assert(fix_wait_view.active_segment == 3);
    assert(!fix_wait_view.fatal);

    set_status(status, BootStage::GPS, BootState::WARN, "timeout warning");
    BootView gps_timeout_view = boot_present(status);
    assert(strcmp(gps_timeout_view.stage_label, "GPS") == 0);
    assert(strcmp(gps_timeout_view.detail_line, "timeout warning") == 0);
    assert(gps_timeout_view.completed_segments == 3);
    assert(gps_timeout_view.active_segment == 3);
    assert(!gps_timeout_view.fatal);

    set_status(status, BootStage::GPS, BootState::OK, "track matched");
    BootView track_view = boot_present(status);
    assert(strcmp(track_view.stage_label, "GPS") == 0);
    assert(strcmp(track_view.detail_line, "track matched") == 0);
    assert(track_view.completed_segments == 3);
    assert(track_view.active_segment == 3);
    assert(!track_view.fatal);

    set_status(status, BootStage::DISPLAY_STAGE, BootState::FAIL, nullptr);
    BootView fail_view = boot_present(status);
    assert(strcmp(fail_view.stage_label, "DISPLAY") == 0);
    assert(strcmp(fail_view.detail_line, "fail") == 0);
    assert(fail_view.completed_segments == 1);
    assert(fail_view.active_segment == 1);
    assert(fail_view.fatal);

    set_status(status, BootStage::READY, BootState::OK, nullptr);
    BootView ready_view = boot_present(status);
    assert(strcmp(ready_view.stage_label, "READY") == 0);
    assert(strcmp(ready_view.detail_line, "ok") == 0);
    assert(ready_view.completed_segments == 4);
    assert(ready_view.active_segment == 4);
    assert(!ready_view.fatal);

    return 0;
}
