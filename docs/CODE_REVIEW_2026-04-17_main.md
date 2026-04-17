# Main-branch code review — 2026-04-17

## Scope

Reviewed `main` at HEAD `b5dcc8e0aac74c698c363af1f161ec6ce6646f58`.

Files reviewed in full:
- `src/types.h`
- `src/session.cpp` / `src/session.h`
- `src/lap_timer/lap_timer_events.cpp`
- `src/lap_timer/lap_timer_crossing.cpp`
- `src/lap_timer/lap_timer_delta.cpp`
- `src/lap_timer/lap_timer_task.cpp`
- `src/lap_timer/lap_timer_internal.h`
- `src/lap_timer.cpp`
- `src/display/display.cpp`
- `src/display/display_driving.cpp`
- `src/display/display_internal.h`
- `src/display/display_format.cpp`
- `src/display/display_status.cpp`
- `src/gps/gps_task.cpp` / `gps_parser.cpp` / `gps_fix.cpp` / `gps_ubx.cpp`
- `src/storage/storage_task.cpp` / `storage_session.cpp` / `storage_vbo.cpp` / `storage_paths.cpp` / `storage_recovery.cpp`
- `src/delta.cpp` / `src/delta.h`
- `src/main.cpp`
- `src/track_runtime.cpp`

## Summary verdict

**WARNING** — no hard crash paths found, but two HIGH-severity correctness bugs exist that will silently produce wrong data at runtime, and a third concurrency hole is latent if the session task ever races the lap_timer task to `session_state` during a stop. The display state machine is clean after the recursion fix. The state machine around recording lifecycle is substantially correct but has a dangling-pointer pattern and two lock-free reads of `session_state` that violate the invariant the comment block itself asserts.

---

## Findings

### [HIGH] Dangling pointer passed to `storage_write_lap_timing` after mutex release

- File: `src/session.cpp:234` and `:270–274`
- What: `lap` is assigned as `&session_state.laps[idx]` while the mutex is held (line 234). The mutex is released at line 270. Immediately after, `storage_write_lap_timing(lap)` is called at line 274 with that now-unprotected pointer. The `session_task` runs on Core 1 and the display task also runs on Core 1 — ordinarily they cannot preempt each other mid-call because FreeRTOS is cooperative at the same priority, but `storage_write_lap_timing` is currently a stub (`(void)lap; return;`). The danger emerges the moment `storage_write_lap_timing` becomes a real implementation that holds `spi_mutex` for tens of milliseconds: during that window `update_session_delta` on Core 0 can take `s_session_mutex` and write into `session_state.laps` through an unrelated path, or the WiFi task (Core 1, lower priority) cannot preempt but a future task could. The deeper bug is that the pointer to internal `SessionState` storage is smuggled out of the mutex guard. If lap-timing write is ever made real, a copy should be taken under the mutex.
- Why it matters: Silent data corruption in the persisted lap record. Not yet observable because the function is stubbed, but will bite the moment real per-lap SD write is implemented.
- Suggested fix: Take a `LapRecord` copy before releasing the mutex:
  ```cpp
  LapRecord lap_copy = *lap;
  xSemaphoreGive(session_mutex);
  storage_write_lap_timing(&lap_copy);
  ```

### [HIGH] Catmull-Rom spline uses `history_get(3)` for both p2 and p3 — degenerate spline, wrong crossing timestamp

- File: `src/lap_timer/lap_timer_crossing.cpp:84–88`
- What: When `s_history_count >= SPLINE_HISTORY` (== 4), `compute_crossing_time` fetches the four control points as indices 1, 2, 3, 3. Both `p2` and `p3` point to the same `GpsPoint`. A Catmull-Rom spline with `p2 == p3` is degenerate: the velocity at p2 is zero, curving the interpolant in a way that has no relation to the real GPS trajectory. The crossing timestamp returned will be systematically wrong whenever four history points exist (i.e., after the second lap start at any session). Index 0 is never read; the correct call is `history_get(0), 1, 2, 3`.
- Why it matters: Wrong `crossing_us` propagates into `LapEvent.crossing_us`, which is then used to compute `lap_time_ms` in `session.cpp:218–219`. Lap times will be subtly wrong on every lap after the first, and the error grows with speed (higher GPS rate = larger timestamp delta per point). This is a correctness bug in the core timing function.
- Suggested fix:
  ```cpp
  const GpsPoint* p0 = history_get(0);  // was history_get(1) — wrong
  const GpsPoint* p1 = history_get(1);  // was history_get(2)
  const GpsPoint* p2 = history_get(2);  // was history_get(3)
  const GpsPoint* p3 = history_get(3);  // was history_get(3) — duplicate
  ```
  Note: `history_get` indexes directly into the fixed circular buffer `s_history[SPLINE_HISTORY]`. When the buffer is full it holds the 4 most recent points at indices 0–3 (oldest first, newest last). Using index 0 gives the oldest control point, which is correct for the Catmull-Rom boundary.

### [HIGH] Lock-free read of `session_state.session_stopped` and `is_recording` on Core 0 without holding the mutex

- File: `src/lap_timer/lap_timer_events.cpp:50`
- What: `handle_finish_crossing` (Core 0, lap_timer task) reads `session_state.session_stopped` and `session_state.is_recording` without taking `s_session_mutex`. `session_stop_recording` (Core 1, session task) writes both of those fields under `session_mutex` at lines 128–134. The ESP32-S3 uses Xtensa LX7 cores; `bool` reads are single-instruction atomic on this architecture for fields at natural alignment, so in practice this does not corrupt data. However the C++ memory model does not guarantee ordering, and the comment at line 47–49 of that file explicitly reasons about the sequencing of these two flags ("stop AFTER first crossing" vs "stop DURING out lap"). A CPU reorder could let the Core 0 reader see `is_recording == false` but `session_stopped == false` (stale), which would fall through to the auto-start branch and call `storage_start_session` during a stop. This is speculative — the specific Xtensa store/load ordering makes it unlikely in practice — but the invariant documented in the comment cannot be statically guaranteed without the lock.
- Suggested fix: Take `s_session_mutex` with a short timeout before reading, or mark both fields `std::atomic<bool>` with `memory_order_acquire` / `memory_order_release`. A 2 ms timeout (matching `update_session_delta`) is sufficient.

### [MEDIUM] `classify_lap` reads `session_state.best_lap_time_ms` without holding the mutex

- File: `src/session.cpp:180, 184`
- What: `classify_lap` is called from `handle_lap_finish` at line 230, which is called from inside the mutex block (mutex is taken at line 222). `classify_lap` then reads `session_state.best_lap_time_ms`. This is actually safe because the mutex is held at the call site. The problem is that `classify_lap` is a free function that accesses the global `session_state` directly with no documentation that it must be called under the mutex, and no assertion to enforce it. A future refactor that moves the call outside the mutex will introduce a silent race.
- Suggested fix: Pass `best_lap_time_ms` as a parameter to `classify_lap` rather than reading the global, eliminating the implicit coupling:
  ```cpp
  static LapStatus classify_lap(int32_t lap_time_ms, int32_t best_lap_time_ms);
  ```

### [MEDIUM] `active_track` global is mutated from three tasks without synchronisation

- File: `src/lap_timer/lap_timer_task.cpp:74–79`, `src/wifi/api_tracks.cpp:326`, `src/lap_timer.cpp:184–185`
- What: `active_track` is a plain `TrackDefinition` global declared in `main.cpp` and exposed via `extern`. The lap_timer task (Core 0) writes it via `track_runtime_sync_detected_track` while holding `s_session_mutex` (line 81), but `s_track` (which points into it) is used without any lock in every subsequent `process_line` call in the same task. The WiFi task `api_tracks.cpp:326` does `memset(&active_track, 0, sizeof(TrackDefinition))` then `lap_timer_reset()` without any inter-task synchronisation. A 120-byte `memset` across two cache lines is not atomic; if the lap_timer task reads `s_track->start_finish` mid-clear it sees a zeroed line geometry and may emit a spurious crossing.
- Suggested fix: Wrap `active_track` mutations in `spi_mutex` or a dedicated track mutex, and let `lap_timer_task` take the same mutex before dereferencing `s_track` when processing lines. Alternatively, use double-buffering: write to a shadow copy, then atomically swap a pointer.

### [MEDIUM] `compute_crossing_time` fallback path uses raw `history_get` indices that can underflow

- File: `src/lap_timer/lap_timer_crossing.cpp:93–94`
- What: In the fallback path (`s_history_count < SPLINE_HISTORY`), the code calls `history_get(s_history_count - 2)` and `history_get(s_history_count - 1)`. `compute_crossing_time` is only reachable from `process_line`, which is only called when `has_prev == true` (lap_timer_task.cpp:102). `has_prev` becomes true after the first GPS fix, which means the earliest `history_count` is 2 when this path is first reachable. So `s_history_count - 2 == 0` is safe. However, `history_get` performs no bounds check (`return &s_history[index]`), and `history_push` uses `s_history[s_history_count]` without checking against `SPLINE_HISTORY` in the fast path (though the slow path memmoves correctly). The safety argument is correct today but fragile; if `process_line` ever gets called earlier, `history_get(-1)` returns `&s_history[-1]` — undefined behaviour, no crash guard.
- Suggested fix: Add an assertion or bounds clamp in `history_get`:
  ```cpp
  const GpsPoint* history_get(int index) {
      if (index < 0 || index >= SPLINE_HISTORY) {
          return &s_history[0];  // safe fallback; crossing will be wrong but won't crash
      }
      return &s_history[index];
  }
  ```

### [MEDIUM] `storage_task` holds `spi_mutex` with `portMAX_DELAY` inside its main loop, blocking display SPI

- File: `src/storage/storage_task.cpp:27`
- What: `storage_task` (Core 1, priority 18) calls `xSemaphoreTake(spi_mutex, portMAX_DELAY)` before every VBO line write. At 25 Hz GPS this is 40 ms period; at 4 MHz SPI and typical VBO line length (~60 bytes) the write takes ~120 µs. However `flush_and_sync` (called every 30 s) holds the mutex for the duration of two SD-layer sync operations, which can spike to several tens of milliseconds. During that window the display task (priority 10, also Core 1) will block trying to take `spi_mutex` with its 10 ms timeout and skip frames. This is the known trade-off and is documented, but the `portMAX_DELAY` on the write means the storage task can never be preempted by a higher-priority Core 1 task that also needs SPI. Since `session_task` (priority 16) does not touch SPI, no deadlock exists today, but the asymmetric priorities combined with `portMAX_DELAY` create a priority-inversion scenario if a new Core 1 task ever needs SPI at priority > 18.
- Suggested fix: Use a bounded timeout for the VBO write (e.g., 50 ms) and log a warning on timeout. Keep `portMAX_DELAY` only for `flush_and_sync`.

### [MEDIUM] `s_track_source` in `track_runtime.cpp` declared `volatile` but used across two cores without a memory barrier

- File: `src/track_runtime.cpp:14`
- What: `s_track_source` is `volatile TrackRuntimeSource`. It is written by the lap_timer task on Core 0 (via `track_runtime_note_auto_detect`) and read by the WiFi task on Core 1 (via `track_runtime_should_apply_late_auto_detect`). `volatile` prevents the compiler from caching the value in a register, but it does not provide a CPU-level memory barrier on a dual-core system. On Xtensa LX7, stores from one core are not guaranteed to be visible to another core without a barrier or cache operation. In practice the L1 data caches of the two cores are coherent via hardware on ESP32-S3, so this is unlikely to cause corruption. But the standard guarantees nothing, and the `volatile` here suggests the author intended cross-core visibility that `volatile` alone cannot provide.
- Suggested fix: Use `portENTER_CRITICAL` / `portEXIT_CRITICAL` around the read/write pair, or convert to `std::atomic<int>` with `memory_order_relaxed` (sufficient for single-variable visibility on a coherent cache system).

### [LOW] `storage_write_lap_timing` is a permanent stub — lap timing section in VBO file will always be empty

- File: `src/storage/storage_session.cpp:87–89`
- What: The function body is `(void)lap; return;`. The VBO `[laptiming]` section header is written at session end but no per-lap rows ever follow. This is presumably intentional debt (sector times written by `write_laptiming_lines` cover the line geometry only, not individual lap records). The concern is that `session.cpp:274` calls this function on every lap finish expecting it to persist data, and the comment at line 273 says "Persist lap to SD" — so there is a documentation / expectation mismatch that will confuse whoever implements this.
- Suggested fix: Either implement the function body or rename it to `storage_write_lap_timing_noop` and add a `// TODO:` comment that explains what the final implementation must do.

### [LOW] VBO line formatting: `lon_amin` sign inversion may be wrong for Eastern hemisphere

- File: `src/storage/storage_vbo.cpp:80`
- What: `double lon_amin = entry->lon_deg * -60.0;` negates the longitude before converting to arc-minutes. The VBO format convention for the `long` column is positive East, but the negation makes positive-East longitudes appear as negative arc-minutes in the file. If the track is in the Eastern hemisphere (Asia, Australia, most of Europe), all longitude values in the VBO file will be sign-inverted. Latitude is not negated (`lat_amin = entry->lat_deg * 60.0`). This asymmetry is suspicious and likely a copy-paste error.
- Suggested fix: Verify the VBO spec's sign convention and adjust. If the spec expects minutes-positive-East, remove the negation: `double lon_amin = entry->lon_deg * 60.0;`.

### [LOW] `gps_fix.cpp` queue-full recovery discards newest fix, not oldest

- File: `src/gps/gps_fix.cpp:54–58`
- What: When the GPS queue is full, the code dequeues one item (the oldest, at the head) and then enqueues the new fix. The comment says it "drops" the oldest when the queue is full, which is correct. However, if the lap_timer task is briefly slow (e.g., during `delta_set_reference` precomputation which yields but still runs on Core 0), this will silently drop fixes rather than applying backpressure. The queue size is 4 (from `main.cpp:475`), which provides ~160 ms of buffer at 25 Hz. If `delta_set_reference` takes longer (4096 points × ~15 µs per haversine = ~60 ms with yields), drops are possible. This is a logging gap rather than a correctness bug — the dropped-fix counter `drops_since_last` is correct and logged every second — but no alarm is raised if sustained drop rates indicate the pipeline is structurally overloaded.

---

## Observations without a finding

**Spline crossing quality vs. linear crossing quality**: The spline path activates only when `s_history_count >= SPLINE_HISTORY` (4 points), but the spline interpolation uses only the interval `[p1, p2]` for timestamp calculation (line 89: `dt = p2->timestamp_us - p1->timestamp_us`). Catmull-Rom is not bounded to `[p1, p2]`; for `u ∈ [0, 1]` the spline can overshoot. This means the returned `crossing_us` could be outside the `[p1.timestamp_us, p2.timestamp_us]` interval on a sharp curve. At normal track speeds this is unlikely to be large enough to matter, but it is a latent source of occasional negative lap times if the overshoot is large enough.

**Session task alternating poll**: `session_task` polls `s_lap_event_q` with a 50 ms timeout and `s_btn_session_q` with zero timeout. If lap events arrive at a sustained rate higher than 1 per 50 ms, button events will be delayed by up to 50 ms per pending lap event. This is not a correctness issue (buttons trigger start/stop, not time-critical events), but it means the recording-stop button could feel sluggish during a lap transition.

**`reset_session_state` called with mutex already held in some paths**: In `session_start_recording`, `reset_session_state()` is called at line 103. `reset_session_state` itself takes `session_mutex` (line 161). But `session_start_recording` then takes the mutex again at line 106. This means there is a lock/unlock/lock sequence rather than a single critical section. Between the two lock acquisitions, another task (display snapshot, WiFi read) could see a partially-reset state: `is_recording == false`, `best_lap_time_ms == -1`, `track_name` still old. This window is tiny (a few microseconds) but exists.

**FW_VERSION hardcoded in `display.cpp:57`**: `const char* FW_VERSION = "v1.0.0"` — no build-time injection from `platformio.ini` or a version header. Will silently display stale version after any update.

**`first_frame` flag in `display_task` is reset unconditionally** even if `render_frame` returns early due to SPI timeout (`display.cpp:442`). This means if the very first frame fails the SPI mutex grab, `first_frame` is cleared and the initial full redraw is never retried. The `s_pending_full_redraw` mechanism partially covers this, but only if `df.full_redraw` was set — on the first frame where `screen_changed = true` it is, so in practice this works out. Still fragile.

---

## Test coverage gap

The `tests_host/` suite has zero coverage of the following paths that generated five regressions in the past two weeks:

1. **Session state machine transitions** — no test for `session_start_recording` → `session_stop_recording` → crossing ignored, or for the out-lap-stop path where `s_first_crossing == true` when stop is called. These are exactly the paths that caused the `session_stopped` field to be added. A host-test harness can mock `storage_start_session` / `storage_end_session` and drive the transition table.

2. **`handle_finish_crossing` auto-start path** — the logic at `lap_timer_events.cpp:64–76` (read `is_recording` without lock, set `is_recording`, call `storage_start_session`) has no test. The condition "what happens if `storage_start_session` fails after `is_recording` is set to true" is covered only by the comment, not by a test.

3. **`classify_lap` boundary conditions** — specifically the case where `best_lap_time_ms == -1` at classification time, and the case where the slow threshold overflows `int32_t` (if `best_lap_time_ms * 150 > INT32_MAX`, which is theoretically impossible for any real lap but not guarded against).

4. **`update_session_delta` is-recording gate** — the `session_state.is_recording ? s_lap_start_us : 0` branch at `lap_timer_delta.cpp:27` has no test. This was the fix for the idle-timer echo loop regression.

5. **`compute_dirty` state transitions** — no test verifies that a `DRIVING_IDLE → DRIVING_OUT_LAP → DRIVING_NORMAL` sequence sets `d.background = true` on each transition. The driving-screen background tests should be easy to host-test since `compute_dirty` operates on pure `SessionState` structs.

The most valuable tests to add, in order of regression risk: (a) crossing-after-stop, (b) SD-fail-during-auto-start, (c) stop-during-out-lap.

---

## What I did NOT review

- `src/wifi/` — WiFi/HTTP server code reviewed only at the points where it touches `session_state` and `active_track`. The full HTTP handler logic, JSON serialisation, and OTA paths were not reviewed.
- `src/boot_*.cpp` — Boot sequence reviewed only for queue/mutex initialisation order in `main.cpp`. The boot log and boot presenter modules were not reviewed.
- `src/button.cpp` — Skimmed for queue usage but not reviewed in detail.
- `src/config.cpp` / `src/track.cpp` — Not reviewed; referenced only for context.
- `src/display/display_lap_list.cpp` — Not reviewed; not in the stated attack surface.
