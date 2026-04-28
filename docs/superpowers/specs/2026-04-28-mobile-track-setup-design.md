# Mobile Track Setup Design

**Date:** 2026-04-28
**Status:** Approved for first implementation slice

## Goal

Make the phone-hosted Web UI good enough to create a trusted start/finish
line at the track without a laptop map. The first version stays fully
offline and runs from the ESP32 access point.

## Current Experience

The firmware Web UI can already create tracks from a phone:

- enter a track name
- mark P1
- mark P2
- review the generated start/finish line
- validate by walking across the line
- save the track

This is logically similar to the laptop `tools/live_map.py` flow, but less
visual. `live_map.py` shows real map tiles, a current-position marker, trail,
line overlays, direction, and crossing candidates. The phone UI currently feels
more like a form plus a small geometry review.

## Product Direction

Do not add online maps in the first slice. A phone connected to the ESP32 AP may
not have reliable internet access, and the ESP32 should not serve map tiles.

Instead, make the phone UI answer the questions that matter on-site:

- What should I do next?
- Is GPS good enough to mark this point?
- Did P1/P2 capture successfully?
- Is the line long enough and pointing the right way?
- Did walking across the line produce PASS events?
- Is it now safe to save?

## First Implementation Slice

The first slice adds a guided offline setup surface inside `Tracks -> Track
Creation`:

1. A setup coach panel that names the next action:
   - enter a track name
   - wait for GPS
   - mark P1
   - walk to the other end and mark P2
   - check direction
   - validate by walking across
   - save

2. A local, offline setup map:
   - current GPS point when available
   - P1 and P2 markers
   - start/finish line
   - crossing direction arrow
   - line length
   - uncertainty circles from sampled point spread

3. Clearer validation feedback:
   - large PASS/REJECT counts
   - remaining accepted crossings needed
   - last candidate reason, such as wrong direction or outside line ends

## Explicit Non-Goals

- No online map tiles.
- No new backend API.
- No WebSocket/SSE.
- No changes to GPS parsing, SD recording, or lap timing behavior.
- No full redesign of the Web UI shell.

## Files

- `src/wifi/web_ui_markup.cpp`: add the setup coach/map containers and small CSS.
- `src/wifi/web_ui_script_track_creation.cpp`: render the coach, setup map, and
  clearer validation state.
- `src/wifi/web_ui_script_track_creation_review.cpp`: reuse projection helpers
  for the offline map if needed.
- `tools/ui_preview/tests/check_firmware_web_ui.js`: regression coverage for the
  guided setup behavior.
- `tools/ui_preview/web_console.js`, `tools/ui_preview/styles.css`,
  `tools/ui_preview/scenarios.js`, and `tools/ui_preview/tests/check_preview.js`:
  keep the static preview aligned with the firmware Track Creation UI.

## Verification

Run after implementation:

```bash
node tools/ui_preview/tests/check_firmware_web_ui.js
node tools/ui_preview/tests/check_preview.js
~/.platformio/penv/bin/pio run
```

On device:

1. Flash firmware.
2. Connect phone to ESP32 WiFi.
3. Open `http://192.168.4.1/`.
4. Go to `Tracks`.
5. Create a test track using P1/P2.
6. Walk across the line until validation passes.
7. Save and confirm the track becomes available.
