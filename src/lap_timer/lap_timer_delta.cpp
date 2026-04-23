#include "lap_timer_internal.h"

#include "../delta.h"
#include "../display.h"
#include "../session.h"

namespace lap_timer_internal {

void update_session_delta(const GpsPoint* curr) {
    int32_t delta_ms = delta_calculate(curr);
    bool valid = delta_is_valid();

    bool wrote = false;
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
        wrote = true;
    }

    // Kick the display task so the driver sees the new delta /
    // position within ~30 ms instead of waiting up to 50 ms for
    // the next render heartbeat.  display_task rate-limits kicks
    // at 30 FPS (MIN_RENDER_INTERVAL_MS) so a 25 Hz GPS fix
    // stream can't starve other core-1 tasks.  We only kick on a
    // successful session_state write: if we skipped because the
    // mutex was busy, the state display would read is stale
    // anyway and the 50 ms heartbeat will pick up the next good
    // write.
    if (wrote) {
        display_kick();
    }
}

}  // namespace lap_timer_internal
