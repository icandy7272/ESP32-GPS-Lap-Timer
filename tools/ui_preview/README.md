# UI Preview Workbench

This directory contains a static desktop workbench for reviewing two UI surfaces side by side:

- the ESP32 Wi-Fi dashboard
- the 320x240 TFT device screen

Both previews are driven by the same scenario registry so UI acceptance and first-pass visual tuning can happen without flashing firmware.

## Running locally

```sh
python3 -m http.server 8000 --bind 127.0.0.1 --directory tools/ui_preview
```

Then open `http://127.0.0.1:8000/` in a browser.

## Scenario source

Shared scenarios live in `tools/ui_preview/scenarios.js`.

Each scenario is the source of truth for both renderers:

- `status`, `sessions`, `tracks`, and `settings` feed the web console preview
- `device` feeds the TFT preview, including boot state, driving state, status details, and raw lap history for the lap-list view

If one preview diverges from the other, update the scenario first unless the issue is renderer-specific.

## Verification flow

From the repo root:

```sh
node tools/ui_preview/tests/check_preview.js
python3 -m http.server 8000 --bind 127.0.0.1 --directory tools/ui_preview
```

Use the smoke test for structural verification, then use the browser preview for visual acceptance.

## When UI diverges from firmware

Check these files first:

- `src/wifi_server.cpp`
  Source of truth for the dashboard hierarchy, labels, and form/button structure.
- `src/display.cpp`
  Source of truth for TFT boot states, delta coloring, status lines, and lap-list presentation.
- `tools/ui_preview/web_console.js`
  Static dashboard renderer mirrored from `src/wifi_server.cpp`.
- `tools/ui_preview/device_screen.js`
  Static TFT renderer mirrored from `src/display.cpp`.

## Manual acceptance checklist

- Verify every scenario button updates both previews.
- Verify `ready-to-drive`, `recording`, and `best-lap-improved` look correct in the web console.
- Verify `cold-boot` and `gps-searching` cover boot states on the device screen.
- Verify `no-gps-driving`, `off-track`, and `recording` cover runtime driving states on the device screen.
- Verify `session-review` shows enough lap rows to judge density and paging behavior.
- Verify `heavy-track-library` stays readable with many sessions and tracks.
- Verify the layout remains readable when the browser width is narrowed.

## References

- `src/wifi_server.cpp`
- `src/display.cpp`
