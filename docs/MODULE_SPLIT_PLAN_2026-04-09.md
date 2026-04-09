# Module Split Plan

> For agentic workers: prefer small extraction commits with no behavior changes bundled into the same commit. Compile after every task.

**Goal:** Reduce Claude read/understand/edit time by breaking the remaining large source files into focused modules with clear responsibilities.

**Architecture:** Keep public module interfaces stable (`gps.h`, `storage.h`, `lap_timer.h`, `track.h`) while moving internal helpers into responsibility-based `.cpp` files plus small `*_internal.h` headers when needed. This is a codebase-shape refactor first, not a behavior rewrite.

**Tech Stack:** PlatformIO, Arduino ESP32, FreeRTOS, TFT_eSPI, SdFat, WebServer

---

## Current State

Largest remaining source files:

- `src/gps.cpp` — 740 lines
- `src/storage.cpp` — 694 lines
- `src/lap_timer.cpp` — 686 lines
- `src/track.cpp` — 619 lines

Already improved enough for now:

- `src/display/` is already split
- `src/wifi/` is already split

Do not spend time re-splitting `display` or `wifi` before the four files above are handled.

---

## Split Principles

1. Split by responsibility, not by arbitrary line count.
2. Target file size: roughly `120-350` lines per `.cpp`.
3. First extraction commit for each module should be behavior-preserving.
4. Do not rename public APIs unless there is a strong reason.
5. Avoid circular includes; keep shared structs in existing headers unless a new internal header is clearly justified.
6. Every extraction step must end with:
   - `pio run -e esp32-s3-devkitc-1`
   - brief note on what moved and what did not change

---

## Recommended Order

1. `gps.cpp`
2. `storage.cpp`
3. `lap_timer.cpp`
4. `track.cpp`
5. Optional cleanup after the four core splits:
   - `types.h`
   - `session.cpp`
   - `main.cpp`

Reason for this order:

- `gps`, `storage`, and `lap_timer` are the biggest context hogs and also the most review-sensitive.
- `track.cpp` is large, but less timing-critical than the first three.
- `session.cpp` and `main.cpp` are still readable enough for now.

---

## Target File Structure

### GPS

Keep:

- `src/gps.h`

Split `src/gps.cpp` into:

- `src/gps/gps_task.cpp`
  - `gps_task()`
  - `gps_init()`
  - queue setup
  - UART task loop
- `src/gps/gps_parser.cpp`
  - checksum
  - field splitting
  - GGA/RMC parsing
  - sentence type detection
  - character accumulator
- `src/gps/gps_fix.cpp`
  - `assemble_point()`
  - `send_fix_if_ready()`
  - RTC sync
  - PPS freshness / timestamp math
- `src/gps/gps_ubx.cpp`
  - UBX frame helpers
  - baud detect
  - rate config
  - NMEA sentence config
  - save-to-flash logic
- `src/gps/gps_internal.h`
  - internal structs like `GgaData`, `RmcData`
  - shared internal helpers/state declarations only if needed

Important:

- Keep PPS ISR and UART/task state boundaries obvious.
- Do not mix parser logic with UBX config logic in the same file after split.

### Storage

Keep:

- `src/storage.h`

Split `src/storage.cpp` into:

- `src/storage/storage_task.cpp`
  - `storage_task()`
  - queue consumption
  - periodic flush decisions
- `src/storage/storage_session.cpp`
  - `storage_start_session()`
  - `storage_end_session()`
  - session state transitions
- `src/storage/storage_vbo.cpp`
  - VBO header writing
  - line formatting
  - lap timing section writing
  - metadata json writing
- `src/storage/storage_paths.cpp`
  - final filename generation
  - collision handling
  - timestamp naming helpers
  - directory sync helpers
- `src/storage/storage_recovery.cpp`
  - tmp recovery
  - startup salvage behavior
- `src/storage/storage_internal.h`
  - shared internal state / constants

Important:

- Keep filename generation and collision logic isolated so it can be reviewed independently.
- Keep recovery logic separate from normal session-finalize logic.

### Lap Timer

Keep:

- `src/lap_timer.h`

Split `src/lap_timer.cpp` into:

- `src/lap_timer/lap_timer_task.cpp`
  - `lap_timer_init()`
  - `lap_timer_task()`
  - `lap_timer_reset()`
  - `lap_timer_set_track()`
- `src/lap_timer/lap_timer_crossing.cpp`
  - line side math
  - spline interpolation
  - crossing time solve
  - debounce
  - arming logic
- `src/lap_timer/lap_timer_events.cpp`
  - finish/sector handlers
  - lap event emission
  - lap validity filtering
- `src/lap_timer/lap_timer_delta.cpp`
  - delta updates
  - session-state propagation
  - VBO forwarder
- `src/lap_timer/lap_timer_internal.h`
  - internal state and helpers

Important:

- `crossing`, `delta`, and `track auto-detect` are logically distinct and should not stay tangled.
- Be careful with file-static state when moving code around.

### Track

Keep:

- `src/track.h`

Split `src/track.cpp` into:

- `src/track/track_store.cpp`
  - `track_init()`
  - `track_save()`
  - `track_delete()`
  - `track_load_first()`
  - ID generation
- `src/track/track_detect.cpp`
  - `track_auto_detect()`
  - distance matching helpers
- `src/track/track_parse.cpp`
  - JSON parsing
  - detection line parsing
  - sector parsing
- `src/track/track_format.cpp`
  - JSON formatting
  - filename/path formatting if needed
- `src/track/track_internal.h`
  - internal declarations and shared constants

Important:

- Keep parsing/formatting separate from storage-side CRUD.
- `track_auto_detect()` should be easy to find and review without opening CRUD logic.

---

## Execution Tasks

### Task 1: Finish GPS Split

**Files:**

- Create: `src/gps/gps_task.cpp`
- Create: `src/gps/gps_parser.cpp`
- Create: `src/gps/gps_fix.cpp`
- Create: `src/gps/gps_ubx.cpp`
- Create: `src/gps/gps_internal.h`
- Modify: `src/gps.cpp` or replace it with a thin compatibility entry if needed
- Modify: `src/gps.h`

- [ ] Extract parser helpers first
- [ ] Move UBX helpers/config flow next
- [ ] Move fix assembly / timestamp / RTC logic
- [ ] Move task/init entrypoints last
- [ ] Run `pio run -e esp32-s3-devkitc-1`
- [ ] Commit only structural changes

### Task 2: Split Storage by Lifecycle

**Files:**

- Create: `src/storage/storage_task.cpp`
- Create: `src/storage/storage_session.cpp`
- Create: `src/storage/storage_vbo.cpp`
- Create: `src/storage/storage_paths.cpp`
- Create: `src/storage/storage_recovery.cpp`
- Create: `src/storage/storage_internal.h`
- Modify: `src/storage.h`

- [ ] Isolate path/collision logic first
- [ ] Move recovery code into its own file
- [ ] Move VBO-format code
- [ ] Move session start/end logic
- [ ] Move task loop last
- [ ] Run `pio run -e esp32-s3-devkitc-1`
- [ ] Commit only structural changes

### Task 3: Split Lap Timer by Domain

**Files:**

- Create: `src/lap_timer/lap_timer_task.cpp`
- Create: `src/lap_timer/lap_timer_crossing.cpp`
- Create: `src/lap_timer/lap_timer_events.cpp`
- Create: `src/lap_timer/lap_timer_delta.cpp`
- Create: `src/lap_timer/lap_timer_internal.h`
- Modify: `src/lap_timer.h`

- [ ] Extract spline/crossing math
- [ ] Extract debounce/arming logic
- [ ] Extract finish/sector handlers
- [ ] Extract delta/session propagation
- [ ] Keep task/init/reset in one file
- [ ] Run `pio run -e esp32-s3-devkitc-1`
- [ ] Commit only structural changes

### Task 4: Split Track Module

**Files:**

- Create: `src/track/track_store.cpp`
- Create: `src/track/track_detect.cpp`
- Create: `src/track/track_parse.cpp`
- Create: `src/track/track_format.cpp`
- Create: `src/track/track_internal.h`
- Modify: `src/track.h`

- [ ] Move auto-detect into its own file
- [ ] Move parsing helpers
- [ ] Move JSON formatting / ID generation
- [ ] Keep CRUD in store file
- [ ] Run `pio run -e esp32-s3-devkitc-1`
- [ ] Commit only structural changes

---

## Guardrails

Do not do these during the split unless required to unblock compilation:

- public API renames
- behavior fixes unrelated to the split
- data structure redesign
- UI changes
- product requirement changes

If a real bug is discovered during extraction:

1. note it
2. finish the structural split cleanly
3. fix the bug in a separate commit

---

## Review Checklist

After each module split, verify:

1. The build still passes.
2. The public header remains easy to understand.
3. Internal helpers are easier to find than before.
4. No new circular include patterns appeared.
5. A reviewer can inspect one concern at a time without opening a 600+ line file.

---

## Definition of Done

This split effort is done when:

- no core `.cpp` file is above roughly `350` lines without strong reason
- `gps`, `storage`, `lap_timer`, and `track` each have clear sub-file ownership
- Claude can inspect one subsystem without loading unrelated logic
- the project still builds with `pio run -e esp32-s3-devkitc-1`

---

## Handoff Note for Claude

Please execute this as a structural refactor, not a product refactor.

Rules:

1. One subsystem at a time
2. One compile after each subsystem
3. Prefer extraction commits before behavior changes
4. If you also need to fix a bug, do it in a follow-up commit, not the same extraction commit
