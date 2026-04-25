# GPS Lap Delta Accuracy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve GPS, lap crossing, and Delta calculation repeatability toward professional kart dash behavior while preserving raw recording integrity.

**Architecture:** Keep the timing pipeline split by consumer: raw fixes stay authoritative for VBO logging and crossing detection, aligned `match_fix` feeds Delta projection, and `display_fix` remains UI-only. First land software-only correctness improvements that can be tested on host; then add receiver-time/NAV-PVT work as a separate protocol upgrade with hardware validation.

**Tech Stack:** ESP32-S3, Arduino/FreeRTOS, u-blox/BK-880 GPS over UART, NMEA/UBX, host C++ tests under `tests_host/`, PlatformIO firmware build.

---

## Scope And Ordering

This plan implements the five review findings in risk order:

1. Route Delta through an aligned `{raw, match_fix, match_valid}` GPS queue payload.
2. Fix crossing timestamp interpolation so confirmed crossings can use the next fix without adding timestamp latency.
3. Add a first physical progress guard to Delta matching.
4. Document and stage receiver epoch / NAV-PVT timing work.
5. Document and stage richer quality gating with GSA or NAV-PVT accuracy fields.

The first three are firmware behavior changes suitable for this development pass. The NAV-PVT and full accuracy-field migration are larger protocol work and should be started after the hot path has tests and field data.

## File Structure

- Modify: `src/types.h`
  - Add a `GpsFixBundle` queue payload carrying raw and aligned match fixes.
- Modify: `src/gps.h`
  - Update GPS queue contract documentation from `GpsPoint` to `GpsFixBundle`.
- Modify: `src/gps/gps_fix.cpp`
  - Package `GpsFilterProcessResult` into `GpsFixBundle` before queue send.
- Create: `src/gps_fix_bundle.h`
  - Declare the pure queue-bundle builder.
- Create: `src/gps_fix_bundle.cpp`
  - Convert a `GpsFilterProcessResult` into the queue payload.
- Modify: `src/main.cpp`
  - Create `gps_queue` with `sizeof(GpsFixBundle)`.
  - Unpack boot-wait GPS fixes from bundle raw points.
- Modify: `src/lap_timer.h`
  - Update queue contract documentation.
- Modify: `src/lap_timer/lap_timer_internal.h`
  - Update `update_session_delta()` signature.
  - Add crossing candidate state needed for delayed timestamp confirmation.
- Modify: `src/lap_timer/lap_timer_task.cpp`
  - Receive `GpsFixBundle`, run crossing/VBO/reference capture on raw, run Delta on aligned match when valid.
- Modify: `src/lap_timer/lap_timer_delta.cpp`
  - Pass the chosen Delta point to `delta_calculate()` while keeping session GPS state based on raw/display paths.
- Modify: `src/lap_timer/lap_timer_crossing.cpp`
  - Store crossing candidates and confirm them one fix later with a centered interpolation window.
- Modify: `src/lap_timer.cpp`
  - Initialize/reset crossing candidate state.
- Create: `src/crossing_time.h`
  - Declare pure crossing timestamp interpolation helpers.
- Create: `src/crossing_time.cpp`
  - Implement signed-distance linear and centered Catmull-Rom crossing timestamps.
- Modify: `src/delta.cpp`
  - Add first-pass progress guard using last accepted progress, current speed, and elapsed time.
- Create: `src/delta_progress_guard.h`
  - Pure helper for bounded forward/backward Delta progress checks.
- Create: `src/delta_progress_guard.cpp`
  - Host-testable implementation.
- Create: `tests_host/test_delta_progress_guard.cpp`
  - Red-green tests for plausible progress, excessive forward jumps, backward jumps, and lap wrap allowance.
- Create: `tests_host/test_gps_fix_bundle.cpp`
  - Red-green tests for raw/match queue payload alignment.
- Create: `tests_host/test_crossing_time.cpp`
  - Red-green tests proving the centered interpolation uses the future point.
- Modify: `docs/ARCHITECTURE.md`
  - Document raw/display/match queue semantics after implementation.
- Modify: `TODOS.md`
  - Mark the queue-bundle item complete and add explicit NAV-PVT follow-up.

## Task 1: Write The Plan Document

**Files:**
- Create: `docs/superpowers/plans/2026-04-25-gps-lap-delta-accuracy.md`

- [x] **Step 1: Capture review findings as staged engineering tasks**

Write this document with clear scope separation between immediate firmware work and later GPS protocol migration.

- [ ] **Step 2: Keep the plan updated while implementing**

As tasks land, update checkbox state and any discovered constraints.

## Task 2: GPS Queue Bundle For Delta `match_fix`

**Files:**
- Modify: `src/types.h`
- Modify: `src/gps.h`
- Modify: `src/gps/gps_fix.cpp`
- Modify: `src/main.cpp`
- Modify: `src/lap_timer.h`
- Modify: `src/lap_timer/lap_timer_internal.h`
- Modify: `src/lap_timer/lap_timer_task.cpp`
- Modify: `src/lap_timer/lap_timer_delta.cpp`

- [x] **Step 1: Write the failing compile/test expectation**

Add `tests_host/test_gps_fix_bundle.cpp` and compile it before `src/gps_fix_bundle.cpp` exists. Expected: compile fails because the helper source does not exist.

Run:

```bash
c++ -std=c++17 -Wall -Wextra -I src -o /tmp/host_test_gps_fix_bundle_red tests_host/test_gps_fix_bundle.cpp src/gps_fix_bundle.cpp
```

- [x] **Step 2: Add `GpsFixBundle`**

Add:

```cpp
typedef struct {
    GpsPoint raw_fix;
    GpsPoint match_fix;
    bool     match_valid;
} GpsFixBundle;
```

The struct belongs next to `GpsPoint` in `src/types.h` because it is the queue contract between GPS and lap timer.

- [x] **Step 3: Package aligned filter output at the producer**

In `gps_send_fix_if_ready()`:

```cpp
GpsFilterProcessResult filter_result = gps_filter_process(point);
GpsFixBundle bundle = {};
bundle.raw_fix = point;
bundle.match_fix = filter_result.match_fix;
bundle.match_valid = filter_result.match_valid;
```

Send/drop `GpsFixBundle`, not `GpsPoint`.

- [x] **Step 4: Unpack bundle at boot and lap timer consumers**

Use `bundle.raw_fix` for boot GPS acquisition, auto-detect, crossing, draft validation, lap point capture, and VBO logging.

- [x] **Step 5: Route only Delta through aligned match fix**

Change `update_session_delta()` to accept raw and Delta input separately:

```cpp
void update_session_delta(const GpsPoint* raw_curr,
                          const GpsPoint* delta_curr);
```

Pass `bundle.match_fix` when `match_valid`, otherwise pass raw as a fail-open fallback.

- [x] **Step 6: Verify**

Run:

```bash
bash tools/run_host_tests.sh
~/.platformio/penv/bin/pio run
```

Expected: host tests pass and firmware build succeeds.

Observed on 2026-04-25: `bash tools/run_host_tests.sh` reported `19 passed, 0 failed`; `~/.platformio/penv/bin/pio run` reported `SUCCESS`.

## Task 3: Centered Crossing Timestamp Confirmation

**Files:**
- Modify: `src/lap_timer/lap_timer_crossing.cpp`
- Modify: `src/lap_timer/lap_timer_internal.h`
- Modify: `src/lap_timer.cpp`

- [x] **Step 1: Add crossing candidate state**

Track, per line:

```cpp
bool     s_crossing_candidate_active[MAX_SECTORS];
GpsPoint s_crossing_candidate_p0[MAX_SECTORS];
GpsPoint s_crossing_candidate_p1[MAX_SECTORS];
GpsPoint s_crossing_candidate_p2[MAX_SECTORS];
double   s_crossing_candidate_expected_sign[MAX_SECTORS];
```

- [x] **Step 2: Replace immediate timestamp calculation with candidate storage**

When a valid side flip is detected, store `prev/curr` and the crossed-side sign. Do not call `debounce_start()` until the next fix.

- [x] **Step 3: Confirm candidate on the next fix**

On the next sample, compute crossing time using `prev_prev`, candidate `prev/curr`, and the next fix. Then start the existing debounce with the backfilled `crossing_us`.

- [x] **Step 4: Use local signed-distance interpolation as fallback**

If the four-point window is unavailable or numerically unstable, use signed-distance linear interpolation on the candidate segment. Prefer robustness over a decorative spline.

- [x] **Step 5: Verify**

Run:

```bash
bash tools/run_host_tests.sh
~/.platformio/penv/bin/pio run
```

Expected: host tests pass and firmware build succeeds. Hardware validation remains required for timing claims.

Observed on 2026-04-25: `bash tools/run_host_tests.sh` reported `20 passed, 0 failed`; `~/.platformio/penv/bin/pio run` reported `SUCCESS`.

## Task 4: Delta Physical Progress Guard

**Files:**
- Create: `src/delta_progress_guard.h`
- Create: `src/delta_progress_guard.cpp`
- Create: `tests_host/test_delta_progress_guard.cpp`
- Modify: `src/delta.cpp`

- [x] **Step 1: Write failing host tests**

Cover:

- plausible forward progress is accepted
- excessive forward progress is rejected
- excessive backward progress is rejected
- near-wrap progress is allowed only near lap boundary

Run:

```bash
bash tools/run_host_tests.sh
```

Expected: new test fails before the helper exists.

- [x] **Step 2: Implement pure progress guard**

Use reference distance, speed, `dt`, and conservative slack:

```cpp
bool delta_progress_is_plausible(double last_progress,
                                 double candidate_progress,
                                 double total_dist_m,
                                 float speed_kmh,
                                 double dt_s);
```

The guard should do nothing when uninitialized or missing timing data.

- [x] **Step 3: Integrate into `delta_calculate()`**

Reject implausible candidates before advancing `s_last_progress`. Keep existing off-track and start/finish ambiguity behavior.

- [x] **Step 4: Verify**

Run:

```bash
bash tools/run_host_tests.sh
~/.platformio/penv/bin/pio run
```

Expected: host tests pass and firmware build succeeds.

Observed on 2026-04-25: `bash tools/run_host_tests.sh` reported `21 passed, 0 failed`; `~/.platformio/penv/bin/pio run` reported `SUCCESS`.

## Task 5: Receiver Epoch / NAV-PVT Follow-Up

**Files:**
- Modify: `docs/ARCHITECTURE.md`
- Modify: `TODOS.md`
- Later likely modify: `src/gps/gps_ubx.cpp`, `src/gps/gps_parser.cpp`, `src/gps/gps_fix.cpp`

- [x] **Step 1: Document current limitation**

State that `timestamp_us` remains UART arrival time in this pass.

- [x] **Step 2: Add explicit follow-up**

Create a TODO for a NAV-PVT spike branch:

- configure UBX NAV-PVT
- parse iTOW, fixType, hAcc, sAcc, headAcc, pDOP
- map receiver epoch to `esp_timer` with an offset smoother
- evaluate baud rate increase from 115200 to 230400/460800

- [x] **Step 3: Do not mix this with Task 2-4**

The protocol migration needs separate hardware validation and should not be hidden inside a Delta refactor.

Observed on 2026-04-25: current UART-arrival limitation is retained in `docs/ARCHITECTURE.md`, and the NAV-PVT spike branch is tracked in `TODOS.md`.

## Task 6: Quality Gating Follow-Up

**Files:**
- Modify: `TODOS.md`
- Later likely modify: `src/gps/gps_ubx.cpp`, `src/gps/gps_fix.cpp`, `src/gps_filter.cpp`, `src/lap_timer/lap_timer_crossing.cpp`, `src/delta.cpp`

- [ ] **Step 1: Decide short-term GSA policy**

Either enable GSA at low cadence and measure UART budget, or keep it disabled and explicitly document quality scoring as HDOP-first.

- [x] **Step 2: Define future NAV-PVT quality fields**

Use receiver-provided accuracy estimates for crossing gates and Delta match scoring.

Observed on 2026-04-25: NAV-PVT quality fields are listed in `TODOS.md`; GSA policy remains an open v1.0 decision.

## Task 7: Field Validation

**Files:**
- Modify: `TODOS.md`

- [x] **Step 1: Add a repeatable crossing test**

At a real line, collect at least 20 passes and compare lap/sector crossing scatter.

- [x] **Step 2: Add a Delta stability test**

Record a short session with repeated laps and inspect Delta jumps near corners, parallel segments, and start/finish wrap.

- [ ] **Step 3: Compare against an external reference**

Use video, stopwatch, or professional dash data if available. Do not claim professional-grade accuracy without field evidence.

Observed on 2026-04-25: crossing scatter and Delta jump review are tracked in `TODOS.md`; external reference comparison remains open for field testing.
