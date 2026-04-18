# GPS Algorithm Optimization Roadmap

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce visible GPS drift and delta instability without sacrificing lap-timing integrity, while preserving a clear upgrade path for future sensor and hardware improvements.

**Architecture:** Treat the current GPS path as three consumers with different needs instead of one shared stream. Keep a minimally processed `raw_fix` path for recording and crossing detection, add a filtered `display_fix` path for UI responsiveness, and introduce a lightly constrained `match_fix` path for delta projection and track-relative progress. Improve fix quality awareness first, then add adaptive filtering, outlier rejection, and continuity constraints in that order.

**Tech Stack:** Arduino, FreeRTOS, existing GPS NMEA parser (`GGA`/`RMC` today), TFT display state, lap timer geometry, PSRAM-backed reference lap storage, host tests and hardware walk tests

---

## Current-State Notes

- The parser currently handles `GGA` and `RMC` only. It exposes fix quality, satellite count, position, speed, and heading, but not `HDOP`, `PDOP`, or broader fix-quality metadata.
- `gps_send_fix_if_ready()` assembles a `GpsPoint` and pushes it directly into the hot path without smoothing, outlier rejection, or confidence scoring.
- `update_session_delta()` writes the current fix straight into `session_state.gps_lat_deg`, `session_state.gps_lon_deg`, and `session_state.speed_kmh`, so the UI mostly reflects raw GPS noise.
- The lap timer already has some geometric protection for crossings: segment-segment intersection, heading window checks, arming distance, and debounce confirmation. That is a good base and should not be destabilized casually.
- The delta engine projects the current point onto the reference polyline and filters candidate segments by heading difference, but it still relies on raw fix continuity more than fix quality.
- Low-speed heading is especially vulnerable because course-over-ground becomes noisy when the kart is walking-speed slow or nearly stationary.
- The current BK-880 path does not expose PPS, so software should focus on reducing visible drift and jumpiness before attempting more ambitious timing claims.

## Decision Summary

### Recommended Direction

- Add a fix-quality model before adding heavier filtering.
- Split GPS consumers into three paths:
  - `raw_fix` for recording, crossing detection, and debugging
  - `display_fix` for status screen, live UI, and perceived stability
  - `match_fix` for delta projection and track-relative progress
- Use stronger smoothing at low speed and lighter smoothing at high speed.
- Reject or downweight obviously implausible fixes instead of trying to smooth every bad point into the solution.
- Add continuity constraints to delta projection so reference matching does not jump across the lap unexpectedly.

### Explicitly Not Recommended As The First Move

- Do **not** run the entire lap-timer and crossing pipeline on a heavily filtered GPS stream.
- Do **not** jump straight to a full Kalman + IMU fusion stack before the codebase can even distinguish display and timing consumers.
- Do **not** treat satellite count alone as an adequate fix-quality metric once `GSA`/DOP data can be exposed.

## File Structure

**Likely Modify**

- `src/gps/gps_internal.h`
- `src/gps/gps_parser.cpp`
- `src/gps/gps_fix.cpp`
- `src/types.h`
- `src/lap_timer/lap_timer_delta.cpp`
- `src/delta.cpp`
- `src/lap_timer/lap_timer_crossing.cpp`
- `src/display/display.cpp`
- `src/display/display_driving.cpp`
- `docs/ARCHITECTURE.md`
- `docs/WIRING.md`
- `TODOS.md`

**Likely Create**

- `src/gps/gps_filter.h`
- `src/gps/gps_filter.cpp`
- `tests_host/test_gps_filter.cpp`
- `tests_host/test_delta_projection.cpp`

**Why this split**

- GPS parsing and fix scoring should stay near the parser and fix assembly layer.
- Filtering needs its own module so display smoothing and matching constraints are testable outside the firmware loop.
- Delta continuity work belongs in the projection layer, not in the display or parser code.

### Task 1: Measure The Drift Before Tuning It

**Files:**
- Modify: `src/gps/gps_fix.cpp`
- Modify: `docs/ARCHITECTURE.md`

- [ ] **Step 1: Add simple GPS diagnostic counters**

Track and print:
- accepted fixes per second
- dropped queue writes
- minimum / maximum reported satellites
- low-speed samples per second
- provisional fix-quality state once that exists

- [ ] **Step 2: Add hardware walk-test logging guidance**

Document repeatable test modes:
- stationary under open sky
- slow walking loop
- track-edge walking with known straight segments

- [ ] **Step 3: Record the baseline behavior in docs**

Write down what "drift" currently means in observable terms:
- visible position jitter when stationary
- heading jumpiness at low speed
- occasional delta jumps or unstable track-relative match

### Task 2: Add A Real Fix-Quality Model

**Files:**
- Modify: `src/gps/gps_internal.h`
- Modify: `src/gps/gps_parser.cpp`
- Modify: `src/gps/gps_fix.cpp`
- Modify: `src/types.h`

- [ ] **Step 1: Parse `GSA` and expose DOP metrics**

Add parsing for `HDOP`, `PDOP`, and `VDOP` if present from the module's sentence mix.

- [ ] **Step 2: Extend the runtime GPS model**

Add fields such as:
- `hdop`
- `pdop`
- `quality_score`
- `quality_tier`
- `heading_reliable`

- [ ] **Step 3: Define a first-pass scoring rule**

Base the score on:
- 3D fix / fix quality
- satellite count
- DOP quality
- speed threshold for heading trust

- [ ] **Step 4: Expose quality in diagnostics and session state**

Do not change any geometry yet. The first goal is observability.

### Task 3: Split Raw, Display, And Match Fix Paths

**Files:**
- Create: `src/gps/gps_filter.h`
- Create: `src/gps/gps_filter.cpp`
- Modify: `src/gps/gps_fix.cpp`
- Modify: `src/lap_timer/lap_timer_delta.cpp`
- Test: `tests_host/test_gps_filter.cpp`

- [ ] **Step 1: Introduce a dedicated filter state module**

The module should explicitly own:
- recent raw fix history
- filtered display state
- filtered match state
- low-speed / stationary state
- outlier counters

- [ ] **Step 2: Keep the original raw fix path available**

Use `raw_fix` for:
- VBO recording
- crossing detection timing
- low-level debugging

- [ ] **Step 3: Route filtered values only to the consumers that need them**

Use:
- `display_fix` for screen/UI values
- `match_fix` for delta projection and progress lookup

Do not use `display_fix` directly for crossing detection.

- [ ] **Step 4: Add host tests for path separation**

Cover cases where:
- raw input jitters slightly but display output stays stable
- one implausible jump is rejected for display/match while raw logging still records the original point

### Task 4: Add Low-Speed Stabilization First

**Files:**
- Modify: `src/gps/gps_filter.cpp`
- Modify: `src/display/display_driving.cpp`
- Test: `tests_host/test_gps_filter.cpp`

- [ ] **Step 1: Add a small sliding median for low-speed position**

Use a short window such as `3-5` samples when speed is below a configured threshold.

- [ ] **Step 2: Add adaptive EMA after the median stage**

Use stronger smoothing at low speed, lighter smoothing at higher speed.

- [ ] **Step 3: Add low-speed heading stabilization**

Below a chosen speed threshold:
- freeze heading, or
- derive it from recent displacement over a larger window instead of trusting instantaneous RMC heading

- [ ] **Step 4: Add a stationary hold state**

When speed and displacement are both very small:
- slow down position updates
- freeze heading
- keep the UI calm instead of reflecting every raw fix wobble

### Task 5: Reject Implausible Points Instead Of Smoothing Everything

**Files:**
- Modify: `src/gps/gps_filter.cpp`
- Modify: `src/gps/gps_fix.cpp`
- Test: `tests_host/test_gps_filter.cpp`

- [ ] **Step 1: Add motion-plausibility checks**

Compare consecutive fixes using:
- implied speed from position delta
- acceleration against recent history
- heading flip severity at low speed

- [ ] **Step 2: Define actions for bad fixes**

For each bad fix, choose one of:
- reject for `display_fix` and `match_fix`
- accept but heavily downweight
- keep raw logging intact

- [ ] **Step 3: Count and expose rejections**

This should remain visible in diagnostics so the tuning does not become guesswork.

### Task 6: Stabilize Delta Projection With Continuity Constraints

**Files:**
- Modify: `src/delta.cpp`
- Modify: `src/lap_timer/lap_timer_delta.cpp`
- Test: `tests_host/test_delta_projection.cpp`

- [ ] **Step 1: Bound the projection search around recent progress**

Favor segments near the last good match more aggressively before falling back to a wider search.

- [ ] **Step 2: Add progress continuity rules**

Guardrails should include:
- maximum forward progress per sample
- bounded backward movement unless a lap-reset condition explains it
- rejection of obviously discontinuous candidate matches

- [ ] **Step 3: Blend geometry quality into the match decision**

Combine:
- lateral distance
- heading agreement
- progress continuity
- current fix-quality tier

- [ ] **Step 4: Add regression tests for jumpy cases**

Cover:
- slow chicane-like motion
- start/finish neighborhood ambiguity
- crossovers or nearby-return geometry

### Task 7: Improve The Reference Lap Instead Of Trusting One Raw Lap Blindly

**Files:**
- Modify: `src/delta.cpp`
- Modify: `src/lap_timer.cpp`
- Test: `tests_host/test_delta_projection.cpp`

- [ ] **Step 1: Add optional reference-lap smoothing**

Apply light smoothing to the stored reference polyline before using it for progress projection.

- [ ] **Step 2: Gate reference promotion by quality**

Avoid promoting obviously noisy laps into the reference set if their quality diagnostics are poor.

- [ ] **Step 3: Reserve a future multi-lap composite path**

Document, but do not yet implement unless needed, a later upgrade where several good laps can produce a cleaner composite reference.

### Task 8: Defer Heavier Sensor Fusion Until The Above Is Proven

**Files:**
- Modify: `docs/WIRING.md`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `TODOS.md`

- [ ] **Step 1: Reserve IMU expansion as a future accelerant, not a blocker**

Document the wiring reservation and the intended role:
- better motion awareness
- heading help at low speed
- smoother UI behavior

- [ ] **Step 2: Defer full Kalman / fusion work**

Only revisit after:
- quality scoring exists
- filtered path separation exists
- low-speed stabilization exists
- delta continuity constraints exist

- [ ] **Step 3: Reassess whether PPS-capable GPS or IMU fusion is still needed**

Do this after the software-only path is measured on real hardware.

## Risk Levels By Change Type

### Low Risk: Display-Only Stability

- `display_fix` smoothing
- low-speed heading freeze for UI
- stationary hold state
- diagnostic counters and quality labels

### Medium Risk: Delta / Match Stability

- `match_fix` smoothing
- outlier rejection for matching
- delta continuity constraints
- reference-lap smoothing

### High Risk: Timing-Core Behavior

- changing the raw fix path used by crossing detection
- changing crossing geometry to rely on heavily filtered positions
- adding large-latency filters before lap-timer event generation

## Rollout Order

1. Task 1: baseline diagnostics
2. Task 2: fix-quality model
3. Task 3: path separation (`raw_fix`, `display_fix`, `match_fix`)
4. Task 4: low-speed stabilization
5. Task 5: outlier rejection
6. Task 6: delta continuity constraints
7. Task 7: reference-lap cleanup
8. Task 8: only then revisit IMU / heavier fusion

## Success Criteria

- Stationary and walking tests show clearly less visible path wobble on the display.
- Low-speed heading no longer swings wildly when motion is minimal.
- Delta and track-relative matching become more continuous and less jumpy.
- Raw logging and crossing detection remain trustworthy and explainable.
- The repo documents which GPS consumers may use filtered data and which must stay close to raw fixes.
