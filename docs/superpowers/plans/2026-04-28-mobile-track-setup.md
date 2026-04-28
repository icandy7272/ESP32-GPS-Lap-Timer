# Mobile Track Setup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve phone-based start/finish creation so it is usable on-site without a laptop map.

**Architecture:** Keep the flow offline inside the existing ESP32-hosted Web UI. Reuse `/api/status` for GPS state and `/api/tracks/draft_validation` for PASS/REJECT validation; add only client-side rendering and copy.

**Tech Stack:** Embedded C++ string builders for HTML/CSS/JS, vanilla browser JavaScript, Node-based Web UI regression tests, PlatformIO firmware build.

---

## File Map

- `src/wifi/web_ui_markup.cpp`: static containers and CSS for the setup coach and local map.
- `src/wifi/web_ui_script_track_creation.cpp`: dynamic rendering for next-step guidance, local setup map, and validation summary.
- `src/wifi/web_ui_script_track_creation_review.cpp`: existing geometry helpers; reuse before adding new math.
- `tools/ui_preview/tests/check_firmware_web_ui.js`: JS behavior tests for the firmware Web UI.
- `tools/ui_preview/web_console.js`: static preview renderer for the Web Console.
- `tools/ui_preview/styles.css`: static preview styles for the coach, local map, and validation chips.
- `tools/ui_preview/scenarios.js`: preview scenario data for guided track review and validation state.
- `tools/ui_preview/tests/check_preview.js`: static preview regression checks.

## Task 1: Add Regression Coverage

- [x] Add a test that renders the setup coach through the P1/P2 flow.
- [x] Add a test that verifies the local map renders current position, P1/P2, line length, and crossing direction.
- [x] Add a test that verifies validation copy shows PASS/REJECT counts and remaining required crossings.
- [x] Run `node tools/ui_preview/tests/check_firmware_web_ui.js`.
- [x] Confirm the new tests fail before implementation.

## Task 2: Add Markup And Styling

- [x] Add `setup-coach` under the track-creation guidance area.
- [x] Add `setup-local-map` near the start/finish controls.
- [x] Add compact CSS for a coach panel, a local map panel, and stronger validation count chips.
- [x] Keep layout mobile-first and avoid introducing a new primary tab or page.

## Task 3: Render Guided Setup State

- [x] Add a helper that computes the next setup action from track name, GPS readiness, P1/P2, validation, and save readiness.
- [x] Render title/body text into `setup-coach`.
- [x] Keep copy terse enough for phone use at the track.
- [x] Re-run `node tools/ui_preview/tests/check_firmware_web_ui.js`.

## Task 4: Render Offline Local Map

- [x] Build the map from current GPS, P1, and P2 in local meter space.
- [x] Show P1/P2 uncertainty circles from `spreadM`.
- [x] Draw the start/finish segment after P2.
- [x] Draw a crossing-direction arrow when heading is available.
- [x] Include a text summary with line length and direction.
- [x] Re-run `node tools/ui_preview/tests/check_firmware_web_ui.js`.

## Task 5: Improve Validation Feedback

- [x] Replace subtle validation text with PASS/REJECT count chips.
- [x] Show remaining accepted crossings needed.
- [x] Show recent candidate reasons in a compact list.
- [x] Keep `Create Track` gating unchanged.
- [x] Re-run `node tools/ui_preview/tests/check_firmware_web_ui.js`.

## Task 6: Final Verification

- [x] Run `node tools/ui_preview/tests/check_firmware_web_ui.js`.
- [x] Run `node tools/ui_preview/tests/check_preview.js`.
- [x] Run `~/.platformio/penv/bin/pio run`.
- [ ] Flash to device and verify on phone.

## Progress Notes

- 2026-04-28: `node tools/ui_preview/tests/check_firmware_web_ui.js` passed.
- 2026-04-28: `node tools/ui_preview/tests/check_preview.js` passed.
- 2026-04-28: `~/.platformio/penv/bin/pio run` passed for `esp32-s3-devkitc-1`.
- 2026-04-28: `~/.platformio/penv/bin/pio run -t upload` did not flash because PlatformIO only saw `/dev/cu.Bluetooth-Incoming-Port`; no ESP32 USB serial port was present in `/dev/cu.*`.
- 2026-04-28 review follow-up: synchronized `tools/ui_preview/web_console.js`,
  `tools/ui_preview/styles.css`, and `tools/ui_preview/scenarios.js` so the
  static preview shows the setup coach, local setup map, and PASS/REJECT
  validation feedback. `node tools/ui_preview/tests/check_preview.js`,
  `node tools/ui_preview/tests/check_firmware_web_ui.js`, and
  `~/.platformio/penv/bin/pio run` passed after this sync.
