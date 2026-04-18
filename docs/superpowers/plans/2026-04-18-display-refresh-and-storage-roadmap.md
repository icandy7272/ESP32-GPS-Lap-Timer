# Display Refresh And Storage Roadmap

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve the on-device display's perceived responsiveness during walking tests and recording sessions without regressing lap-timing reliability or painting the hardware into a corner before soldering.

**Architecture:** Keep the GPS timing path unchanged on Core 0, but decouple "data arrives" from "large TFT redraw happens". Prioritize software changes that reduce per-frame pixel traffic, then move storage writes from per-fix SD commits toward PSRAM-backed batching. Treat hardware SPI separation and sensor expansion as reserved upgrade paths rather than immediate blockers.

**Tech Stack:** Arduino, FreeRTOS, TFT_eSPI, SdFat, ESP32-S3 N16R8 flash + PSRAM, existing host tests and hardware walk tests

---

## Current-State Notes

- Display refresh is explicitly throttled to `100 ms` per frame (`10 FPS` target) in `src/display/display_internal.h`.
- The driving screen pushes a full `320 x 196` sprite for the center region instead of updating only the changed glyphs, so each visual update moves a large amount of pixel data through the TFT SPI bus.
- TFT and SD card currently share the same SPI wiring, and display rendering yields when storage owns the SPI mutex. This protects recording durability, but it makes display drop frames during active SD writes.
- The walking-test profile changes lap-detection thresholds only. It does not change display cadence, TFT SPI speed, or storage behavior.
- The current low-speed speed display rounds to integer km/h before deciding whether the center region is dirty, so walking-speed changes can appear stale even when new GPS points are arriving.
- GPS is configured for up to `25 Hz`, which means new fixes arrive roughly every `40 ms`. That is good enough for smoother perceived animation, but not enough to claim true `10 ms` measurement updates.
- The firmware already contains a short in-memory queue (`vbo_write_queue`, depth `256`) before SD writes. That queue is not a full-session recorder; it only absorbs short bursts while storage catches up.
- Using ESP32 internal flash as a recording-time cache is not the preferred direction: default SPIFFS space is limited, flash writes are not real-time friendly, and flash activity can interfere with normal execution more than PSRAM buffering would.

## Decision Summary

### Recommended Direction

- Improve perceived refresh in software first:
  - show more low-speed detail
  - add visible live-ness indicators
  - reduce redraw area
  - raise display cadence only after redraw cost drops
- Replace "write each fix to SD immediately" with "buffer in PSRAM and write SD in batches" once software-only UI work lands.
- Keep real-time SD durability as a tunable policy by adding periodic flush checkpoints instead of buffering an entire session in volatile memory.
- If the device is not yet soldered, reserve the option to move SD to a separate SPI host later, but do not block current work on that hardware change.

### Explicitly Not Recommended As The Next Step

- Do **not** use ESP32 internal flash / SPIFFS as the primary recording-time cache.
- Do **not** chase a literal `10 ms` screen update target before cutting the amount of TFT pixel traffic.
- Do **not** redesign the lap-timer / GPS timing path just to make the UI feel faster.

## File Structure

**Likely Modify**

- `src/display/display_internal.h`
- `src/display/display.cpp`
- `src/display/display_driving.cpp`
- `src/storage/storage_internal.h`
- `src/storage/storage_task.cpp`
- `src/storage/storage_session.cpp`
- `src/main.cpp`
- `src/pins.h`
- `docs/WIRING.md`
- `docs/ARCHITECTURE.md`
- `TODOS.md`

**Likely Create**

- `src/storage/storage_buffer.h`
- `src/storage/storage_buffer.cpp`
- `tests_host/test_storage_buffer.cpp`
- `docs/superpowers/specs/<future-dated-display-spec>.md` if a deeper UI redesign is approved later

**Why this split**

- Display files should own perceptual responsiveness changes and dirty-region logic.
- Storage buffering should be isolated in a dedicated module so the current recording pipeline stays understandable.
- Wiring and architecture docs should capture any hardware reservations before soldering locks the layout in.

### Task 1: Document And Measure The Current Bottlenecks

**Files:**
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/WIRING.md`
- Modify: `src/display/display.cpp`

- [ ] **Step 1: Add lightweight display diagnostics**

Record enough runtime counters to answer:
- attempted display frames per second
- skipped frames because `session_mutex` was busy
- skipped frames because `spi_mutex` was busy
- full redraw count vs partial redraw count

- [ ] **Step 2: Surface the counters in serial diagnostics**

Reuse the existing once-per-second diagnostic style so walk tests can tell whether the "slow" feeling is UI throttling, SPI contention, or both.

- [ ] **Step 3: Update architecture docs**

Document the measured behavior and make it explicit that the current UI target is `10 FPS`, not a true high-refresh display path.

- [ ] **Step 4: Verify on hardware**

Walk-test and record a short session while watching serial logs.

Expected:
- clear visibility into how often display frames are skipped
- enough evidence to compare before and after future UI work

### Task 2: Improve Perceived Refresh Without Hardware Changes

**Files:**
- Modify: `src/display/display_internal.h`
- Modify: `src/display/display.cpp`
- Modify: `src/display/display_driving.cpp`

- [ ] **Step 1: Make low-speed motion visible**

Change the walking-speed UI behavior so speeds below a chosen threshold show one decimal place instead of integer-only km/h.

- [ ] **Step 2: Add a live GPS heartbeat indicator**

Add a very small visual element that updates on every accepted GPS fix or display snapshot cycle so the user can see the device is alive even when the large numeric value is not changing much.

- [ ] **Step 3: Split center-screen redraw work into smaller regions**

Avoid pushing the entire center sprite when only a few glyphs changed. Prefer focused redraws for:
- speed digits
- units / labels only when state changes
- lap time / status widgets independently

- [ ] **Step 4: Revisit frame cadence after redraw cost drops**

Only after smaller redraws are in place, test a display interval in the `40-50 ms` range and confirm it improves perceived smoothness without increasing dropped frames.

- [ ] **Step 5: Verify on hardware**

Compare three cases:
- idle / ready screen
- walking-speed out-lap screen
- active recording with SD writes

Expected:
- display feels more alive even at low speed
- display cadence can increase modestly without becoming less stable

### Task 3: Add A PSRAM-Backed Storage Buffer And Batch SD Writes

**Files:**
- Create: `src/storage/storage_buffer.h`
- Create: `src/storage/storage_buffer.cpp`
- Modify: `src/storage/storage_internal.h`
- Modify: `src/storage/storage_task.cpp`
- Modify: `src/storage/storage_session.cpp`
- Test: `tests_host/test_storage_buffer.cpp`

- [ ] **Step 1: Add a dedicated storage buffer abstraction**

Create a focused module that owns:
- PSRAM-backed ring allocation
- high-water and low-water thresholds
- batch drain helpers
- overflow accounting

- [ ] **Step 2: Write host tests for the buffer behavior**

Cover:
- push / pop ordering
- wraparound
- high-water signaling
- overflow policy
- draining partial batches

- [ ] **Step 3: Change storage writes from per-fix to batched drains**

Have `storage_task` pull several buffered entries per SD lock acquisition instead of one entry per SPI transaction.

- [ ] **Step 4: Keep durability explicit**

Choose and document one of these policies before implementation:
- periodic checkpoint flush every few seconds
- high-water forced flush
- stop-recording full drain plus final sync

The goal is to avoid "whole session lost on reset" while still reducing SPI contention significantly.

- [ ] **Step 5: Verify on hardware**

Measure:
- display frame skips during recording before vs after batching
- queue / ring occupancy
- tail-drain behavior at stop
- behavior during simulated slow SD writes

Expected:
- fewer SPI lock handoffs
- smoother display during recording
- no silent tail-data loss at stop

### Task 4: Reserve A Hardware Upgrade Path Before Soldering

**Files:**
- Modify: `docs/WIRING.md`
- Modify: `src/pins.h`
- Modify: `TODOS.md`

- [ ] **Step 1: Reserve an alternate SD SPI routing option**

Before soldering permanently, document and if possible expose pads, headers, or jumper options so SD can be moved to a separate SPI host later without reworking the whole build.

- [ ] **Step 2: Keep TFT wiring optimized for speed**

Document that TFT SPI lines should stay short and clean because higher TFT SPI clocks are more valuable to perceived refresh than any flash-caching workaround.

- [ ] **Step 3: Reserve an IMU / I2C expansion header**

Leave a simple expansion path for:
- `3V3`
- `GND`
- `SDA`
- `SCL`
- optional interrupt pin

This is not required for the next UI pass, but it preserves the option to add high-rate motion feedback later.

- [ ] **Step 4: Record the GPS limitation explicitly**

Document that the current BK-880 path does not expose PPS, so future "professional instrument" behavior should focus on UI responsiveness first and only later consider a PPS-capable GPS upgrade if timing precision goals increase.

### Task 5: Defer Advanced Upgrades Until The Software-Only Pass Lands

**Files:**
- Modify: `TODOS.md`
- Modify: `docs/ARCHITECTURE.md`

- [ ] **Step 1: Defer internal-flash caching**

Mark internal flash / SPIFFS session caching as rejected for now unless a future partition redesign and reliability study justify it.

- [ ] **Step 2: Defer full-sensor fusion work**

Do not add IMU-driven smoothing, wheel speed, or CAN-style data blending until the display path has already been improved using the existing GPS-only architecture.

- [ ] **Step 3: Reassess after the first two tasks**

After Task 2 and Task 3, decide whether the remaining pain justifies:
- separate SD/TFT SPI buses
- a PPS-capable GPS module
- IMU-assisted UI animation

## Rollout Order

1. Task 1: measure the current problem clearly
2. Task 2: ship software-only perceived-refresh gains
3. Task 3: reduce SD-vs-display contention with PSRAM-backed batching
4. Task 4: preserve hardware options before soldering
5. Task 5: only then revisit larger hardware and sensor upgrades

## Success Criteria

- Walking-speed tests no longer feel visually stale when the kart is moving slowly.
- Recording sessions produce noticeably fewer display hiccups while preserving session data integrity.
- The repo documents why PSRAM buffering is preferred over internal-flash caching for this device.
- Wiring guidance preserves a practical path to separate SD and TFT buses later.
