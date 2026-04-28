# Code Review — 2026-04-28 Mobile Track Setup

## Scope

Working-tree changes against `origin/main` for the "Mobile Track Setup"
slice from `docs/superpowers/specs/2026-04-28-mobile-track-setup-design.md`
and `docs/superpowers/plans/2026-04-28-mobile-track-setup.md`.

- 8 files modified, +1169 / −32.
- Firmware: `src/wifi/web_ui.cpp`, `src/wifi/web_ui_markup.cpp`,
  `src/wifi/web_ui_script_track_creation.cpp`.
- Preview parity: `tools/ui_preview/web_console.js`,
  `tools/ui_preview/styles.css`, `tools/ui_preview/scenarios.js`.
- Tests: `tools/ui_preview/tests/check_firmware_web_ui.js`,
  `tools/ui_preview/tests/check_preview.js`.

## Verification

```
node tools/ui_preview/tests/check_firmware_web_ui.js   # PASS
node tools/ui_preview/tests/check_preview.js           # PASS
```

Per the plan, `~/.platformio/penv/bin/pio run` was already run on
2026-04-28 and on-device flashing is still pending (no ESP32 USB serial
port was visible). That last item is not actionable from a code review.

## Outcome

**PASS with non-blocking polish items.** No CRITICAL or HIGH findings.
The streaming dashboard rewrite, the offline setup coach/local map, and
the PASS/REJECT validation feedback all match the design doc and have
real regression tests. The items below are concrete adjustments codex
can make without rethinking the design.

## Resolution Update - 2026-04-28

Codex applied the actionable review items M1, M2, L1, L2, and L3 in the
current working tree, plus the follow-up validation-summary readiness
parity issue found after M1:

- Preview now normalizes `track_creation.repeatability`, blocks Create
  Track while repeatability is active, and mirrors the firmware coach /
  validation-summary blocker copy.
- Firmware validation summary now says `Ready to Save` only when
  `isCreateReady()` is true; otherwise it surfaces the active
  repeatability or incomplete-sector blocker.
- The firmware setup coach escapes generated title/body text before
  assigning `innerHTML`.
- The firmware JS fragment extractor test helper is bounded to the
  requested C++ function body and has a regression test for adjacent
  raw-string fragment functions.
- The duplicated setup-map helpers now carry explicit sync comments,
  and the preview projection path has the same empty-point guard as
  firmware.

Verification rerun after the resolution:

```
node tools/ui_preview/tests/check_firmware_web_ui.js   # PASS
node tools/ui_preview/tests/check_preview.js           # PASS
git diff --check                                       # PASS
~/.platformio/penv/bin/pio run                         # SUCCESS
```

---

## Findings

### M1 — Setup coach + isCreateReady parity gap in web_console.js (MEDIUM)

**Files**

- `tools/ui_preview/web_console.js`

**Where**

- `setupCoachContent(draft)` around `tools/ui_preview/web_console.js:706`.
- `isCreateReady(draft)` around `tools/ui_preview/web_console.js:651`.
- Compare with firmware:
  - `setupCoachContent` in `src/wifi/web_ui_script_track_creation.cpp:226`.
  - `isCreateReady` in `src/wifi/web_ui_script_core.cpp:159`.

**Problem**

The firmware coach has a dedicated `repeatability.active` branch and
`isCreateReady` explicitly requires `!_trackDraft.repeatability.active`.
The preview's `setupCoachContent` and `isCreateReady` don't reference
`repeatability` at all, and `normalizeTrackCreationState` never copies
a `repeatability` field onto `draft`. Today no scenario sets that state
so nothing visibly breaks, but:

- Anyone writing a future preview scenario with an active repeatability
  check will get a "Ready to Save" coach line and an enabled Create
  button in the preview while the firmware would correctly block both.
- The firmware test `testMobileSetupCoachExplainsActiveRepeatabilityBeforeReady`
  has no preview-side counterpart, so the divergence won't be caught
  even if the scenario is added later.

**Recommended fix**

In `tools/ui_preview/web_console.js`:

1. In `normalizeTrackCreationState`, normalize a `repeatability` block on
   `draft`, mirroring the existing `normalizeValidation`. At minimum:

   ```js
   repeatability: {
     active: Boolean(source.repeatability && source.repeatability.active),
     status: String((source.repeatability && source.repeatability.status) || ""),
   }
   ```

2. In `isCreateReady(draft)`, AND in `!draft.repeatability.active`.

3. In `setupCoachContent(draft)`, before the `isCreateReady(draft)`
   branch, add:

   ```js
   if (draft.repeatability.active) {
     return {
       title: "Step 3 - Finish Repeatability",
       body: "Finish the repeatability check before saving.",
     };
   }
   ```

4. In `tools/ui_preview/tests/check_preview.js`, add a scenario with
   `repeatability.active = true` and assert the coach shows
   "Finish Repeatability" / "before saving" and the Create button stays
   disabled. Keep the assertion language aligned with the firmware
   test in `check_firmware_web_ui.js`.

---

### M2 — `extractJsFragment` parser is brittle (MEDIUM)

**Files**

- `tools/ui_preview/tests/check_firmware_web_ui.js:178-225`

**Problem**

The new parser locates the function-body terminator with
`source.lastIndexOf(')JS";')` against the entire .cpp file. Today every
`web_ui_script_*.cpp` has exactly one `build_web_ui_script_*_fragment`
function so this works. But:

- Adding a second function returning a raw JS literal in the same file
  silently makes the parser pick the wrong end and concatenate code from
  two functions.
- A `// )JS";` comment or a regular C++ string ending in `)JS";` would
  also break the parser. Today none exists, but it's a foot-gun.

**Recommended fix**

Bound the search to the function body. Smallest viable change:

1. After finding the function's open brace via the existing match,
   walk forward keeping a brace counter to find the matching closing
   brace `}` — call its index `functionEnd`.
2. Use `source.lastIndexOf(')JS";', functionEnd)` instead of the
   unbounded `lastIndexOf`.
3. Assert `finalRawCloseIndex < functionEnd` to fail loudly if a future
   refactor breaks the assumption.

This keeps the parser local to the function and makes the failure mode
obvious.

---

### L1 — Coach panel innerHTML uses unescaped concatenation in firmware (LOW)

**Files**

- `src/wifi/web_ui_script_track_creation.cpp` — `renderSetupCoach`
  around the `panel.innerHTML='<div class="setup-coach-title">'+coach.title+'</div>'+...` line.

**Problem**

`coach.title` and `coach.body` are currently 100% hardcoded English
strings (`'Step 1 - Name'`, `'Walk to the other end of the line. ...'`).
No XSS risk today. But:

- `escapeHtml` already exists in the dashboard fragment
  (`src/wifi/web_ui_script_dashboard.cpp:31`) and is loaded before this
  fragment in the streaming order, so it's available at runtime.
- The web_console preview already runs `coach.title` and `coach.body`
  through `escapeHtml` (`tools/ui_preview/web_console.js:788-789`).
- Without this guard, a future change that pipes `_trackDraft` user
  input (e.g., the track name) into the coach body silently introduces
  XSS in firmware while staying safe in the preview.

**Recommended fix**

Wrap both fields:

```js
panel.innerHTML='<div class="setup-coach-title">'+escapeHtml(coach.title)+'</div>'+
  '<div class="setup-coach-body">'+escapeHtml(coach.body)+'</div>';
```

No behavior change today, defense in depth for tomorrow.

---

### L2 — `setupMapProjection` duplicated between firmware and preview (LOW)

**Files**

- `src/wifi/web_ui_script_track_creation.cpp` — `setupMapProjection`,
  `projectSetupMapPoint`, `setupMapUncertaintySvg`, `setupMapMarkerSvg`,
  `renderSetupLocalMap`.
- `tools/ui_preview/web_console.js` — `setupMapProjection`,
  `projectSetupMapPoint`, `setupMapUncertaintyMarkup`,
  `setupMapMarkerMarkup`, `renderSetupLocalMap`.

**Problem**

These are line-for-line duplicates with only minor cosmetic differences
(`Number.isFinite` vs `isFinite`, attribute escaping). The duplication
is intentional — firmware and preview run separate JS — but the cost is
that any bug fix needs to be replicated.

**Recommended fix**

Add a one-line comment at the top of each duplicated function in both
files saying "MUST stay in sync with the matching function in <other
file path>". No code refactor; just make the maintenance contract
visible. Example:

```js
// MUST stay in sync with setupMapProjection in
// src/wifi/web_ui_script_track_creation.cpp.
function setupMapProjection(points, width, height, padding) { ... }
```

Skip if the team would rather find duplicates by grep.

---

### L3 — `setupMapProjection` defensive guard asymmetry (LOW)

**Files**

- `src/wifi/web_ui_script_track_creation.cpp` — firmware
  `setupMapProjection` starts with `if(!points.length){return null;}`.
- `tools/ui_preview/web_console.js` — preview `setupMapProjection`
  has no equivalent guard.

**Problem**

Both call sites already check `points.length` before invoking
`setupMapProjection`, so the divergence isn't a runtime bug. But the
preview version walks `points.forEach` over an empty array and ends up
with `Infinity / -Infinity` bounds and a non-finite scale. The
`!Number.isFinite(scale) || scale <= 0` guard catches scale, but the
returned projection still has `Infinity` bounds and downstream
`projectSetupMapPoint` math becomes `NaN`. Defense in depth would help
the preview survive a future caller that forgot the length check.

**Recommended fix**

Add the same early return to the preview function:

```js
function setupMapProjection(points, width, height, padding) {
  if (!points.length) {
    return null;
  }
  var origin = localOriginPoint(points);
  ...
}
```

And in `renderSetupLocalMap`, treat a null projection as the existing
"Waiting for GPS or marked points" state (an additional safety belt; the
length check is already there).

---

## Optional polish (not required)

### N1 — Fragment overload set in web_ui.cpp

`send_dashboard_content` is overloaded three ways in
`src/wifi/web_ui.cpp` (char\*, char\*+len, String&). Clear but verbose.
A single template would compress this to one signature; not blocking.

### N2 — Rename the LIT macros

`WEB_UI_SHORT_LINE_MIN_LENGTH_M_LIT` and `WEB_UI_MIN_ACCEPTED_CROSSINGS_LIT`
in `src/wifi/web_ui_script_core.cpp:9-22` are stringified-int macros so
they can be embedded in a raw JS literal. The `_LIT` suffix is fine; the
new test parser hardcodes both names in
`tools/ui_preview/tests/check_firmware_web_ui.js:198-199`. If you ever
add a third such macro, also update that hardcoded list.

---

## Non-issues confirmed

- The streaming dashboard (`server.setContentLength(CONTENT_LENGTH_UNKNOWN)`
  + `sendContent` in 2 KB chunks + final `sendContent("", 0)`
  terminator) is correct chunked-transfer usage and a real heap-pressure
  win. Peak heap drops from "all fragments concatenated" to "the
  largest single fragment" (track_creation, ~30 KB).
- All helpers referenced by the new code (`localOriginPoint`,
  `projectPointMeters`, `pointSpreadMeters`, `lineConfidenceInfo`,
  `headingArrow`, `compassLabel`, `formatCoord`,
  `reviewLineLengthMeters`, `normalizeHeading`, `MIN_ACCEPTED_CROSSINGS`,
  `makeValidationState`, `isPointSamplingActive`,
  `samplingProgressMessage`, `canMarkWithCurrentGps`,
  `gpsMarkingBlockMessage`, `hasIncompleteSectors`, `isCreateReady`,
  `isNameReady`, `currentGpsPoint`, `hasValidFix`) exist in
  `src/wifi/web_ui_script_core.cpp` or
  `src/wifi/web_ui_script_track_creation_review.cpp`.
- The firmware's `validate-summary` chips and the preview's
  `web-console__validation-chip` markup both render PASS/REJECT counts,
  remaining-needed copy, and recent reject reasons. The new tests
  exercise both.
- `isCreateReady` in the firmware
  (`src/wifi/web_ui_script_core.cpp:159`) already gates on
  `validation.accepted >= MIN_ACCEPTED_CROSSINGS`, `!repeatability.active`,
  and `!hasIncompleteSectors()`. The new coach branches don't relax
  that gate.

---

## Summary for Codex

Apply M1, M2, L1, L2, L3 in `tools/ui_preview/web_console.js`,
`tools/ui_preview/tests/check_preview.js`,
`tools/ui_preview/tests/check_firmware_web_ui.js`, and
`src/wifi/web_ui_script_track_creation.cpp`. After each change, re-run:

```
node tools/ui_preview/tests/check_firmware_web_ui.js
node tools/ui_preview/tests/check_preview.js
~/.platformio/penv/bin/pio run
```

Skip N1/N2 unless you want to bundle them with the M/L set.
