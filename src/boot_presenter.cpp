#include "boot_presenter.h"

#include <cstring>

#include "boot_status.h"

namespace {

constexpr char kStorageWarnLabel[] = "STORAGE WARN";
constexpr char kUnknownStageLabel[] = "UNKNOWN";
constexpr char kUnknownStateLabel[] = "unknown";
constexpr uint8_t kMaxStageSegment =
    static_cast<uint8_t>(BootStage::READY);

const char* stage_name_from_stage(BootStage stage) {
    static const char* const names[] = {"POWER", "DISPLAY", "STORAGE", "GPS", "READY"};
    const size_t idx = static_cast<size_t>(stage);
    if (idx < (sizeof(names) / sizeof(names[0]))) {
        return names[idx];
    }
    return kUnknownStageLabel;
}

const char* state_name_from_state(BootState state) {
    static const char* const names[] = {"start", "ok", "warn", "fail"};
    const size_t idx = static_cast<size_t>(state);
    if (idx < (sizeof(names) / sizeof(names[0]))) {
        return names[idx];
    }
    return kUnknownStateLabel;
}

uint8_t stage_to_segment(BootStage stage) {
    int idx = static_cast<int>(stage);
    if (idx < 0) {
        return 0;
    }
    if (idx > static_cast<int>(kMaxStageSegment)) {
        return kMaxStageSegment;
    }
    return static_cast<uint8_t>(idx);
}

bool is_storage_recovery(const BootStatus& status) {
    return status.stage == BootStage::STORAGE &&
           status.state == BootState::WARN &&
           std::strcmp(status.display_detail, "session recovered") == 0;
}

}  // namespace

BootView boot_present(const BootStatus& status) {
    BootView view{};
    const uint8_t segment = stage_to_segment(status.stage);
    view.completed_segments = segment;
    view.active_segment = segment;
    view.fatal = (status.state == BootState::FAIL);

    view.stage_label =
        is_storage_recovery(status) ? kStorageWarnLabel : stage_name_from_stage(status.stage);

    view.detail_line = (status.display_detail[0] != '\0')
                           ? status.display_detail
                           : state_name_from_state(status.state);

    return view;
}
