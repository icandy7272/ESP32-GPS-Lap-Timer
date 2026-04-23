#include "lap_timer_internal.h"

#include "../delta.h"
#include "../display.h"
#include "../session.h"

namespace lap_timer_internal {

void update_session_delta(const GpsPoint* curr) {
    int32_t delta_ms = delta_calculate(curr);
    bool valid = delta_is_valid();

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        session_state.delta_ms = delta_ms;
        session_state.delta_valid = valid;
        session_state.off_track = delta_is_off_track();
        session_state.gps_fix_ok = curr->fix_3d;
        session_state.gps_satellites = curr->satellites;
        session_state.gps_lat_deg = curr->lat_deg;
        session_state.gps_lon_deg = curr->lon_deg;
        session_state.speed_kmh = curr->speed_kmh;
        // Only echo the lap-start timestamp into session state while we
        // are actually recording.  Otherwise the READY / idle screen
        // would see a running timer (session_stop_recording() zeroes
        // current_lap_start_us, but without this gate the delta engine
        // writes s_lap_start_us back on the very next GPS fix).
        session_state.current_lap_start_us =
            session_state.is_recording ? s_lap_start_us : 0;
        xSemaphoreGive(s_session_mutex);
    }

    // Kick the display task unconditionally so the driver sees the
    // fresh GPS position + delta digit within ~30 ms of the fix.
    // Codex review 2026-04-23 Low: kick even when the session_mutex
    // write above skipped, because display_task's GPS overlay reads
    // from gps_filter_get_display_fix() (independent of session
    // state) and the filter already has the updated fix at the
    // point update_session_delta runs.  Falling back to the 50 ms
    // heartbeat on every session-contention blip was defeating the
    // fast path exactly when mutex contention was highest.
    //
    // display_task rate-limits kicks at 30 FPS
    // (MIN_RENDER_INTERVAL_MS) so a 25 Hz GPS stream can't starve
    // other core-1 tasks.
    display_kick();
}

}  // namespace lap_timer_internal
