# Boot Observability And Splash Design

## Summary

This spec defines a unified boot-status system for the ESP32-S3 GPS device so that:

- cold boot progress is visible on both serial and TFT
- the splash screen appears immediately after power-on
- the user can tell whether boot is progressing or stuck
- serial logs and on-screen state are driven from the same source of truth

The visual direction is intentionally restrained:

- pure black background
- centered QuanYo Racing logo
- very small stage label
- minimal five-step progress indicator
- no decorative gradients or extra copy on the device screen

## Goals

- Show a branded splash as early as possible after power-on.
- Make cold-boot failures diagnosable without guessing whether the problem is TFT, SD, GPS, or something earlier.
- Keep the on-device boot UI product-like and uncluttered.
- Preserve the current firmware behavior where non-critical subsystems may still allow degraded runtime.

## Non-Goals

- Building a full debug dashboard on the TFT.
- Showing detailed timestamps, heap, or low-level errors on the splash screen.
- Changing runtime screen design outside the boot experience.
- Introducing a new animation-heavy visual style.
- Including WiFi AP / HTTP server startup in the boot-stage model; WiFi remains a post-`READY` concern.

## User Experience

### Boot Screen Layout

The selected direction is:

- pure black background
- centered QuanYo Racing logo, slightly larger than the earliest mockups
- bottom status zone separated from the logo area
- one tiny current-stage label
- one short secondary status line
- one minimal five-segment progress bar

The boot screen should feel calm and intentional rather than flashy. It should look acceptable even under slight font or scaling differences, so the layout must keep explicit vertical safety margins.

### Screen Content Rules

The TFT boot screen should show only:

- logo
- current coarse stage
- short status detail
- five-segment progress indicator

The TFT boot screen should not show:

- extra captions like "SYSTEM STARTUP"
- long paragraphs
- detailed diagnostics
- multiple independent labels competing for attention

## Boot Status Model

Create a lightweight boot-status module that acts as the shared source of truth for:

- current boot stage
- current boot state
- short display detail
- optional detailed serial detail
- stage start time

Suggested coarse on-screen stages:

- `POWER`
- `DISPLAY`
- `STORAGE`
- `GPS`
- `READY`

Suggested boot states:

- `start`
- `ok`
- `warn`
- `fail`

### Fatal vs Recoverable Outcomes

Use this rule:

- `fail` means boot cannot safely continue and the TFT should remain on that stage
- `warn` means the subsystem degraded but boot may continue

This preserves current behavior where, for example, storage may fail but the device can still run in a reduced mode.

## Screen Stage Mapping

The TFT should stay coarse-grained even if serial logs are more detailed.

Mapping:

- `POWER`
  - early pin safe-state
  - rail stabilization delay
- `DISPLAY`
  - TFT reset pulse
  - TFT init
  - splash ready
- `STORAGE`
  - SD mount
  - config load
  - track load
  - crash-log persistence and recovery detection
- `GPS`
  - UART start
  - GPS task start
  - fix wait
  - track auto-detect after a fix
  - optional short track confirmation while keeping the coarse stage at `GPS`
- `READY`
  - start runtime tasks
  - hand off to normal UI

Track auto-detect does not become a sixth on-screen stage in v1. The progress bar stays five segments and track detection is expressed through the `GPS` detail text and serial logs.

If `storage_recovered` is set, the new boot UI should treat it as `STORAGE warn` rather than a separate full-screen stage. Keep the unified splash layout visible, change the detail line to `session recovered`, and hold that message briefly before advancing to `GPS`. The initial hold may match the current behavior (`2000 ms`) to preserve visibility.

Failure variants on screen should be short:

- `DISPLAY FAIL`
- `STORAGE FAIL`
- `GPS FAIL`

Recoverable warnings may either:

- keep the current stage label and change the detail line, or
- temporarily show `STORAGE WARN` if that still fits the visual budget

Implementation should prefer the simpler option that best preserves layout stability.

The progress bar is intentionally coarse. It is acceptable for `POWER`, `DISPLAY`, and `STORAGE` to complete quickly and for `GPS` to visually hold much longer. During long waits, the detail line is the primary progress signal.

## Serial Log Format

Serial logging should use one shared helper so that boot output is structured and easy to scan.

Format:

```text
[BOOT][0012 ms][POWER][ok] rails stable
[BOOT][0148 ms][DISPLAY][start] resetting TFT
[BOOT][0296 ms][DISPLAY][ok] splash ready
[BOOT][0418 ms][STORAGE][start] mounting SD
[BOOT][0479 ms][STORAGE][ok] config loaded
[BOOT][0635 ms][GPS][start] starting UART and GPS task
[BOOT][8120 ms][GPS][ok] 3D fix acquired (12 sats)
[BOOT][8205 ms][GPS][ok] track matched: autodetect complete
[BOOT][8350 ms][READY][ok] entering runtime
```

Requirements:

- timestamp is boot-relative in milliseconds
- stage token matches the coarse stage model
- state token is one of `start`, `ok`, `warn`, `fail`
- detail text is short but useful

## Display API Direction

Split boot display responsibilities from runtime display responsibilities.

Suggested shape:

- `display_boot_init()`
  - initialize TFT hardware as early as possible
  - render the selected splash layout once
  - enable the backlight immediately after the first splash frame is drawn
- `display_boot_update(...)`
  - update only the current stage label, detail line, and progress state
- `display_init(...)`
  - keep existing runtime display initialization and hand-off for the main UI

This lets the device show a branded boot screen before storage, GPS, and runtime tasks are fully online.

The approved logo and black background should be drawn once in `display_boot_init()`. Subsequent updates should be partial redraws of the mutable status area rather than full-screen rerenders.

To reduce migration risk, the existing `display_show_*` boot helpers may temporarily survive as thin wrappers around the new boot renderer. The steady-state direction is still one unified boot layout driven by `display_boot_init()` and `display_boot_update(...)`.

## Asset Handling

The current approved logo source came from a local PNG provided by the user.

Implementation should not depend on that external path at runtime. Instead, the implementation plan should convert the approved logo into a repo-local display asset suitable for TFT rendering, for example:

- a converted RGB565 bitmap array
- a compressed image asset loaded from flash
- another TFT-friendly embedded format

The implementation should preserve the approved visual proportions while staying practical for firmware size and startup latency.

The target on-screen presentation should match the accepted preview direction: centered on the 320x240 boot screen at roughly `210-214 px` visible width, scaled proportionally within the available logo area.

Prefer a repo-local embedded asset so `DISPLAY` does not depend on SD availability. A tightly cropped PROGMEM-friendly asset is preferred for v1; keep the flash cost modest (roughly within a few tens of KiB rather than turning the splash into a large decode pipeline).

## Failure Behavior

### Fatal Failures

If boot reaches a fatal failure:

- keep the screen on the failing stage
- show a short failure label and one short reason line
- emit full serial detail
- do not silently advance to `READY`

Examples:

- `DISPLAY FAIL`
- `STORAGE FAIL`
- `GPS FAIL`

### Recoverable Warnings

If boot can continue in degraded mode:

- emit a serial `warn`
- keep TFT messaging brief
- continue boot flow after the warning

The device should not look frozen in a recoverable case.

### Stage Outcome Rules

For v1, do not invent arbitrary watchdog deadlines for every stage unless there is a concrete subsystem contract to enforce. Keep the boot outcome rules aligned with the current firmware behavior:

- `POWER` uses the existing stabilization delay and then advances normally.
- `DISPLAY` is fatal if the boot display path cannot complete; do not add a speculative extra timeout unless testing proves a real need.
- `STORAGE` remains recoverable when `storage_init()` fails; boot continues without SD-backed features.
- `GPS` keeps the current explicit `30000 ms` wait budget. Timeout is `warn`, not `fail`, and boot continues without a fix.
- Track auto-detect miss is not a boot failure; keep the `GPS` to `READY` flow moving with a short detail update or confirmation.

### Fatal Failures In V1

For v1 fatal failures:

- keep the backlight on
- keep the failure screen visible until power-cycle or manual reset
- do not auto-retry

Backlight dimming or automatic retry can be treated as a future enhancement rather than part of the first observability pass.

## Implementation Boundaries

- Add one focused boot-status module rather than scattering raw `Serial.println(...)` calls through `setup()`.
- Keep runtime display code mostly unchanged outside the boot entry points.
- Do not redesign runtime screens as part of this work.
- Keep the splash implementation simple and deterministic.
- Reuse existing cold-boot timing improvements already added for TFT and SPI safe-state handling.
- Keep SPI access serialized during boot. Any SD transaction should finish before a TFT boot update touches the bus; do not draw the boot screen from inside an active SD critical section.
- Feed the existing crash breadcrumb / crash-log path with the last coarse boot stage reached so post-mortem logs can say where boot stopped.
- Keep WiFi AP + HTTP server startup explicitly out of scope for the boot-stage UI and serial stage model because it begins after `READY`.

## Verification Plan

### Manual Verification

- Perform several full power-off to power-on cold boots.
- Verify the TFT shows splash immediately and advances through stages without white-screen stalls.
- Test without SD card and confirm behavior is explicit rather than ambiguous.
- Test the recovery path and confirm `STORAGE warn` / `session recovered` is visible before `GPS`.
- Test slow GPS acquisition and confirm the TFT clearly remains in `GPS`.
- Test GPS fix plus track auto-detect and confirm track messaging stays within the `GPS` stage rather than adding a sixth coarse stage.
- Confirm serial logs and TFT stages remain aligned.

### Regression Coverage

- Keep existing host-side tests green.
- Add at least one small boot-status unit test that verifies coarse stage mapping and state transitions.
- Ensure any new boot-status helper is testable without requiring Arduino runtime where practical.

## Assumptions

- The approved visual style is the latest restrained mockup: black background, larger centered logo, tiny stage text, minimal progress bar.
- The screen should remain simple enough to read at a glance in a motorsport context.
- The first implementation should prioritize reliability and observability over animation or polish details.

## Review Resolution

_Reviewed 2026-04-13 against current codebase and Claude review feedback._

Accepted into the spec:

- Fold track auto-detect and optional track confirmation into the `GPS` stage instead of adding a sixth coarse stage.
- Represent `storage_recovered` as a `STORAGE warn` message inside the unified boot layout, with a short fixed hold before `GPS`.
- Make the progress bar explicitly coarse and rely on the detail line as the main progress signal during long GPS waits.
- Require backlight enable immediately after the first splash frame is drawn.
- Clarify the migration path: draw the static logo once, then do partial redraws; existing boot helpers may temporarily act as wrappers.
- Pin down asset direction: repo-local embedded logo, no SD dependency, and an approximate approved on-screen width.
- Make boot-time SPI ordering and crash-breadcrumb integration explicit.
- State explicitly that WiFi is post-`READY` and out of scope.
- Define fatal-failure behavior as persistent on-screen until power-cycle or manual reset in v1.

Intentionally not adopted as written:

- The suggested new `DISPLAY 500 ms` and `STORAGE 2000 ms` watchdog deadlines were not added verbatim. Current code has one concrete explicit boot timeout today: `GPS` wait at `30000 ms`, which already behaves as `warn`. `DISPLAY` and `STORAGE` currently report outcomes from their underlying init paths, so the spec keeps that behavior unless later testing produces evidence for a hard per-stage deadline.

## Next Step

After review of this spec, create an implementation plan covering:

- boot-status module shape
- display boot API changes
- serial log helper integration
- stage mapping in `setup()`
- asset conversion and rendering strategy
- test additions
