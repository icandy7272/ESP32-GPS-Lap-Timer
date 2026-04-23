# Codex Review - 2026-04-23 GPS Precision Follow-up

## Scope

- Reviewed recent GPS/precision commits on `main`, especially:
  - `598e9fb feat(gps): alpha-beta Kalman smoothing + route filtered fix to live map`
  - `83eb29e feat(gps): CFG-NAV5 minElev=10 + staticHoldThresh=5 for drift suppression`
- Re-read the current hot-path files around GPS parsing, filtering, display, live map, delta, and Web status.
- Verification run:
  - `./tools/run_host_tests.sh`
  - Result: `18 passed, 0 failed`

## Findings

### 1. High: stale-baseline escape hatch no longer fully resets the display path

- Files:
  - `src/gps_filter.cpp:418-435`
  - `src/gps_filter.cpp:438-474`
- Problem:
  - The consecutive-reject escape hatch now clears `display_valid` / `match_valid`, but it does **not** reset:
    - `s_state.kalman_display`
    - `raw_history_count / raw_history_next`
  - That means the next "accepted after reset" display sample is **not** a true fresh seed anymore:
    - the Kalman state can still predict from stale pre-jump state
    - the display median can still be polluted by stale pre-jump history on low-speed paths
- Why this matters:
  - The code comments still describe this path as "first-after-reset" rebuild behavior, but that is no longer true after the Kalman change.
  - The existing host test only covers a high-speed jump (`speed > DISPLAY_MEDIAN_MAX_SPEED_KMH`), so it does not catch the low-speed stale-history case.
- Recommendation:
  - When the escape hatch fires, also:
    - `gps_kalman_reset(&s_state.kalman_display);`
    - clear display raw-history state
  - Add a host test that reproduces:
    - low-speed rejected spike sequence
    - reset firing
    - next accepted sample below `DISPLAY_MEDIAN_MAX_SPEED_KMH`
    - expectation: first recovered display fix seeds from the new sample instead of staying biased toward old history

### 2. Medium: Web/phone status path is still on raw session GPS, so walking drift remains visible there

- Files:
  - `src/lap_timer/lap_timer_delta.cpp:13-21`
  - `src/wifi/api_status.cpp:48-53`
  - `src/display/display.cpp:79-89`
- Problem:
  - TFT display now reads filtered `display_fix`.
  - `[gps-live]` serial stream now also emits filtered lat/lon.
  - But `/api/status` still returns `session_state.gps_lat_deg` / `gps_lon_deg`, and those fields are still written from raw `curr` in `update_session_delta()`.
- Why this matters:
  - This leaves the user with three different consumer paths:
    - TFT: filtered
    - serial live map: filtered
    - phone/Web UI: raw
  - That is the most likely explanation for "walking test on phone still looks very floaty" even after the recent display/live-map smoothing work.
- Recommendation:
  - Make the API policy explicit:
    - either return filtered display coordinates from `/api/status`
    - or expose both raw and filtered coordinates so UI code can choose intentionally
  - If track creation should remain conservative, expose both:
    - raw for debugging / timing-adjacent logic
    - filtered for operator-facing map/coordinate display

## Important Open Gaps

### 3. Delta path still does not consume `match_fix`

- Files:
  - `src/lap_timer/lap_timer_task.cpp:167`
  - `src/lap_timer/lap_timer_delta.cpp:9-21`
  - `src/gps_filter.cpp:505-516`
- Observation:
  - The filter module already produces a `match_fix`, but delta is still calculated from raw `curr`.
- Impact:
  - Recent work improves TFT stability and serial live-map stability.
  - It does **not** yet fully improve delta stability.
- Recommendation:
  - Keep crossing detection and VBO logging on raw fixes.
  - Route only delta/matching through `match_fix`, as the roadmap already intends.

### 4. Quality model still does not get the full GSA signal mix in normal runtime

- Files:
  - `src/gps/gps_ubx.cpp:97-99`
  - `src/gps/gps_parser.cpp:164-177`
  - `src/gps/gps_fix.cpp:39-52`
- Observation:
  - There is a GSA parser, but default runtime config still disables GSA output.
  - So the quality model usually falls back to GGA HDOP and does not consistently get `fix_type` / `pdop` / `vdop`.
- Impact:
  - The quality framework exists, but it is still not operating with the richest per-fix metadata.
- Recommendation:
  - Decide whether the UART budget can afford enabling GSA at a low cadence.
  - If not, document clearly that the current quality tier is "HDOP-first / partial metadata", not full DOP-aware scoring.

## Suggested Next Steps For Claude

1. Fix the stale-reset bug in `gps_filter.cpp`.
2. Add a focused host test for low-speed reset recovery.
3. Decide and implement a `/api/status` policy for filtered vs raw coordinates.
4. Wire `match_fix` into delta only, without touching crossing detection raw-path behavior.
5. Re-run `./tools/run_host_tests.sh`.

## Suggested Prompt For Claude

Please continue from the review findings in this file.

Priority order:

1. Fix the high-severity reset bug in `src/gps_filter.cpp`:
   - when the consecutive-reject escape hatch fires, also reset the display Kalman state and clear display raw-history state
   - add a host test that specifically covers low-speed recovery after reset

2. Then address the Web/UI path inconsistency:
   - `/api/status` still returns raw session GPS while TFT and `[gps-live]` now use filtered coordinates
   - choose and implement an explicit API policy (filtered only, or both raw + filtered fields)

3. After that, evaluate routing delta through `match_fix` only:
   - do not move crossing detection or VBO logging off the raw path

Please keep the review findings above in mind and verify with `./tools/run_host_tests.sh` before claiming completion.
