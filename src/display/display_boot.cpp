// ============================================================
// Boot sequence renderer (called before display_task starts)
// Draws a single static frame and partially redraws status rows.
// ============================================================

#include "display.h"
#include "display_internal.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <stdio.h>
#include <string.h>

#include "boot_logo_asset.h"
#include "boot_presenter.h"

namespace {

constexpr int kProgressSegmentCount = 5;
constexpr int kStatusZoneTop = 170;
constexpr int kStatusZoneHeight = SCREEN_H - kStatusZoneTop;
constexpr int kStageRowY = kStatusZoneTop + 12;
constexpr int kDetailRowY = kStatusZoneTop + 30;
constexpr int kProgressY = kStatusZoneTop + 46;
constexpr int kProgressH = 8;
constexpr int kProgressMarginX = 40;
constexpr int kProgressGap = 6;
constexpr int kMaxStageChars = 24;
constexpr int kMaxDetailChars = 64;

constexpr uint16_t kCompletedColour = 0x7BEF;
constexpr uint16_t kInactiveColour = 0x2104;

bool s_boot_frame_drawn = false;
bool s_boot_view_cached = false;
BootState s_cached_state = BootState::START;
uint8_t s_cached_completed = 0;
uint8_t s_cached_active = 0;
char s_cached_stage[kMaxStageChars] = {0};
char s_cached_detail[kMaxDetailChars] = {0};

uint16_t accent_colour(BootState state) {
    switch (state) {
        case BootState::OK:
            return TFT_GREEN;
        case BootState::WARN:
            return TFT_YELLOW;
        case BootState::FAIL:
            return TFT_RED;
        case BootState::START:
        default:
            return kCompletedColour;
    }
}

uint16_t detail_colour(BootState state) {
    switch (state) {
        case BootState::START:
            return TFT_DARKGREY;
        case BootState::WARN:
            return TFT_YELLOW;
        case BootState::FAIL:
            return TFT_RED;
        case BootState::OK:
        default:
            return TFT_WHITE;
    }
}

void copy_text(char* dst, size_t dst_len, const char* src) {
    if (dst_len == 0) {
        return;
    }
    const char* safe_src = (src != nullptr) ? src : "";
    snprintf(dst, dst_len, "%s", safe_src);
}

void draw_stage_label(const char* stage_label, BootState state) {
    s_tft.fillRect(0, kStatusZoneTop + 2, SCREEN_W, 18, TFT_BLACK);
    s_tft.setTextDatum(MC_DATUM);
    s_tft.setTextColor(accent_colour(state), TFT_BLACK);
    s_tft.drawString(stage_label, SCREEN_W / 2, kStageRowY, 2);
}

void draw_detail_line(const char* detail, BootState state) {
    s_tft.fillRect(0, kStatusZoneTop + 20, SCREEN_W, 16, TFT_BLACK);
    s_tft.setTextDatum(MC_DATUM);
    s_tft.setTextColor(detail_colour(state), TFT_BLACK);
    s_tft.drawString(detail, SCREEN_W / 2, kDetailRowY, 1);
}

void draw_progress_bar(uint8_t completed, uint8_t active, BootState state) {
    const int total_gap_w = kProgressGap * (kProgressSegmentCount - 1);
    const int usable_w = SCREEN_W - (2 * kProgressMarginX) - total_gap_w;
    const int segment_w = usable_w / kProgressSegmentCount;
    const int progress_w = (segment_w * kProgressSegmentCount) + total_gap_w;

    s_tft.fillRect(kProgressMarginX - 1, kProgressY - 1, progress_w + 2, kProgressH + 2, TFT_BLACK);

    for (int i = 0; i < kProgressSegmentCount; ++i) {
        uint16_t colour = kInactiveColour;
        if (i < static_cast<int>(completed) || i < static_cast<int>(active)) {
            colour = kCompletedColour;
        }
        if (i == static_cast<int>(active)) {
            colour = accent_colour(state);
        }
        const int x = kProgressMarginX + (i * (segment_w + kProgressGap));
        s_tft.fillRect(x, kProgressY, segment_w, kProgressH, colour);
    }
}

void publish_compat_status(BootStage stage, BootState state, const char* detail) {
    BootStatus status = boot_status_make();
    boot_status_begin(&status, stage, 0);
    boot_status_update(&status, state, detail, detail);
    display_boot_update(status);
}

}  // namespace

void draw_boot_static_frame() {
    s_tft.fillScreen(TFT_BLACK);

    const int logo_x = (SCREEN_W - static_cast<int>(kBootLogoWidth)) / 2;
    const int logo_y = (kStatusZoneTop - static_cast<int>(kBootLogoHeight)) / 2;
    s_tft.pushImage(logo_x, logo_y, kBootLogoWidth, kBootLogoHeight, kBootLogoPixels);

    s_tft.fillRect(0, kStatusZoneTop, SCREEN_W, kStatusZoneHeight, TFT_BLACK);

    s_boot_frame_drawn = true;
    s_boot_view_cached = false;
}

void draw_boot_status(const BootStatus& status) {
    if (!s_boot_frame_drawn) {
        draw_boot_static_frame();
    }

    const BootView view = boot_present(status);
    const bool state_changed = !s_boot_view_cached || s_cached_state != status.state;

    if (state_changed || strcmp(s_cached_stage, view.stage_label) != 0) {
        draw_stage_label(view.stage_label, status.state);
    }
    if (state_changed || strcmp(s_cached_detail, view.detail_line) != 0) {
        draw_detail_line(view.detail_line, status.state);
    }
    if (state_changed || s_cached_completed != view.completed_segments ||
        s_cached_active != view.active_segment) {
        draw_progress_bar(view.completed_segments, view.active_segment, status.state);
    }

    s_cached_state = status.state;
    s_cached_completed = view.completed_segments;
    s_cached_active = view.active_segment;
    copy_text(s_cached_stage, sizeof(s_cached_stage), view.stage_label);
    copy_text(s_cached_detail, sizeof(s_cached_detail), view.detail_line);
    s_boot_view_cached = true;
}

void display_boot_update(const BootStatus& status) {
    if (!s_boot_frame_drawn) {
        display_boot_init();
    }
    draw_boot_status(status);
}

void display_show_splash() {
    display_boot_init();
    publish_compat_status(BootStage::DISPLAY_STAGE, BootState::OK, "splash ready");
}

void display_show_gps_search(int sats) {
    char detail[32];
    if (sats > 0) {
        snprintf(detail, sizeof(detail), "%d sats", sats);
        publish_compat_status(BootStage::GPS, BootState::START, detail);
        return;
    }
    publish_compat_status(BootStage::GPS, BootState::START, "waiting for fix");
}

void display_show_track_found(const char* name) {
    char detail[kBootStatusDisplayDetailChars];
    if (name != nullptr && name[0] != '\0') {
        snprintf(detail, sizeof(detail), "track %s", name);
        publish_compat_status(BootStage::GPS, BootState::OK, detail);
        return;
    }
    publish_compat_status(BootStage::GPS, BootState::OK, "track matched");
}

void display_show_recovery() {
    publish_compat_status(BootStage::STORAGE, BootState::WARN, "session recovered");
}

void display_show_ready() {
    publish_compat_status(BootStage::READY, BootState::OK, "entering runtime");
}
