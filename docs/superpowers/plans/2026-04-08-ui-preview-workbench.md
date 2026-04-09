# UI Preview Workbench Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a static browser-based workbench that lets us review both the ESP32 web console UI and the TFT device screen UI on a desktop, driven by shared scenarios and ready for first-pass UI polish.

**Architecture:** Add a lightweight `tools/ui_preview/` static app built with vanilla HTML/CSS/JS. Keep one shared scenario registry, render the web console and device screen previews from that same state, and expose a small Node-based smoke test that validates scenarios and markup without introducing a frontend framework or build step.

**Tech Stack:** Static HTML/CSS/JavaScript, CommonJS-compatible browser scripts, Node built-ins (`assert`, `fs`, `path`) for smoke tests, Python `http.server` for local preview, PlatformIO remains unchanged.

---

## File Map

### New files

- `tools/ui_preview/index.html`
  Main workbench shell with roots for web console, device screen, and scenario controls.
- `tools/ui_preview/styles.css`
  Shared layout, preview chrome, responsive rules, and device-frame styling.
- `tools/ui_preview/app.js`
  Browser bootstrap: load default scenario, wire control buttons, re-render both previews.
- `tools/ui_preview/scenarios.js`
  Shared scenario registry and fallback helpers.
- `tools/ui_preview/web_console.js`
  Web console preview renderer aligned to `src/wifi_server.cpp` information architecture.
- `tools/ui_preview/device_screen.js`
  Device screen preview renderer aligned to `src/display.cpp` screens and state rules.
- `tools/ui_preview/README.md`
  Run instructions, source-of-truth references, and manual verification checklist.
- `tools/ui_preview/tests/check_preview.js`
  Node smoke test covering scenario registry, fallback behavior, and key renderer output.

### Existing files to inspect while implementing

- `src/wifi_server.cpp`
  Current web dashboard structure, labels, data fields, and interactions.
- `src/display.cpp`
  Current TFT screens, boot states, formatting, and status rules.
- `docs/superpowers/specs/2026-04-08-ui-preview-workbench-design.md`
  Approved feature spec.

### Optional follow-up files if implementation reveals a need

- `tools/ui_preview/assets/`
  Only create if preview needs dedicated static illustrations. Do not create in the first pass without need.

---

### Task 1: Scaffold the Static Workbench and Smoke Test

**Files:**
- Create: `tools/ui_preview/index.html`
- Create: `tools/ui_preview/styles.css`
- Create: `tools/ui_preview/app.js`
- Create: `tools/ui_preview/README.md`
- Create: `tools/ui_preview/tests/check_preview.js`

- [ ] **Step 1: Write the failing smoke test**

Create `tools/ui_preview/tests/check_preview.js` with a minimal file/layout contract first:

```js
const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const indexHtml = fs.readFileSync(path.join(root, "index.html"), "utf8");

assert.match(indexHtml, /id="web-console-root"/);
assert.match(indexHtml, /id="device-screen-root"/);
assert.match(indexHtml, /id="scenario-controls-root"/);
console.log("preview scaffold ok");
```

- [ ] **Step 2: Run the smoke test to confirm it fails**

Run: `node tools/ui_preview/tests/check_preview.js`  
Expected: FAIL with `ENOENT` because `index.html` does not exist yet.

- [ ] **Step 3: Add the minimal workbench scaffold**

Create `tools/ui_preview/index.html` with:

- A document title like `UI Preview Workbench`
- Three root containers:
  - `web-console-root`
  - `device-screen-root`
  - `scenario-controls-root`
- Script tags for `scenarios.js`, `web_console.js`, `device_screen.js`, and `app.js`

Create `tools/ui_preview/styles.css` with:

- Base page layout
- A two-panel desktop layout
- A stacked mobile layout
- Neutral shell styling only for now

Create `tools/ui_preview/app.js` with a safe boot stub:

```js
(function () {
  window.UiPreviewApp = {
    init() {
      // Safe bootstrap so the page can load before render modules are wired.
    },
  };

  window.addEventListener("DOMContentLoaded", () => {
    window.UiPreviewApp.init();
  });
})();
```

Create `tools/ui_preview/README.md` with:

- What this tool is for
- How to run it with `python3 -m http.server`
- Which source files it mirrors (`src/wifi_server.cpp` and `src/display.cpp`)

- [ ] **Step 4: Re-run the smoke test**

Run: `node tools/ui_preview/tests/check_preview.js`  
Expected: PASS and print `preview scaffold ok`.

- [ ] **Step 5: Commit the scaffold**

```bash
git add tools/ui_preview/index.html tools/ui_preview/styles.css tools/ui_preview/app.js tools/ui_preview/README.md tools/ui_preview/tests/check_preview.js
git commit -m "feat: scaffold UI preview workbench"
```

---

### Task 2: Add the Shared Scenario Registry and Control Surface

**Files:**
- Create: `tools/ui_preview/scenarios.js`
- Modify: `tools/ui_preview/index.html`
- Modify: `tools/ui_preview/app.js`
- Modify: `tools/ui_preview/styles.css`
- Modify: `tools/ui_preview/tests/check_preview.js`

- [ ] **Step 1: Extend the failing smoke test for scenarios**

Update `tools/ui_preview/tests/check_preview.js` to require `scenarios.js` and assert:

- At least 8 scenarios exist
- These IDs exist:
  - `cold-boot`
  - `gps-searching`
  - `ready-to-drive`
  - `recording`
  - `best-lap-improved`
  - `off-track`
  - `session-review`
  - `heavy-track-library`
- There is a fallback helper for unknown scenario IDs

Example:

```js
const scenariosApi = require(path.join(root, "scenarios.js"));
assert.ok(Array.isArray(scenariosApi.SCENARIOS));
assert.ok(scenariosApi.SCENARIOS.length >= 8);
assert.equal(scenariosApi.getScenarioById("missing").id, "ready-to-drive");
```

- [ ] **Step 2: Run the test and confirm it fails**

Run: `node tools/ui_preview/tests/check_preview.js`  
Expected: FAIL with `Cannot find module '../scenarios.js'` or missing export assertions.

- [ ] **Step 3: Implement the shared scenario registry**

Create `tools/ui_preview/scenarios.js` as a CommonJS/browser-compatible module. Export:

- `SCENARIOS`
- `DEFAULT_SCENARIO_ID`
- `getScenarioById(id)`

Structure each scenario so both previews can use the same source of truth:

```js
const SCENARIOS = [
  {
    id: "ready-to-drive",
    label: "Ready To Drive",
    status: { gps_fix: true, satellites: 10, recording: false, current_lap: 1, best_lap_ms: 52380, track: "Ningbo Kart Center" },
    sessions: [],
    tracks: [],
    settings: { wifi_ssid: "GPS-LapTimer", wifi_pass: "12345678", brightness: 200 },
    device: { screen: "driving", delta_ms: 0, delta_valid: false, off_track: false, lap_count: 0, best_lap_number: -1, current_lap_time_ms: 0, boot_state: null }
  }
];
```

Use a UMD-style wrapper so browser scripts can read `window.UiPreviewScenarios` and the Node smoke test can `require()` the same file.

- [ ] **Step 4: Implement the scenario control bar**

Update `app.js` to:

- Load `ready-to-drive` by default
- Render a button per scenario into `scenario-controls-root`
- Store `activeScenarioId`
- Re-render both preview panels whenever the active scenario changes

Update `styles.css` so:

- Control buttons are clearly separated from product UI
- Active scenario is obvious
- Controls wrap cleanly on smaller widths

- [ ] **Step 5: Re-run the test**

Run: `node tools/ui_preview/tests/check_preview.js`  
Expected: PASS with scenario assertions succeeding.

- [ ] **Step 6: Serve the page and verify it loads**

Run: `python3 -m http.server 8000 --directory tools/ui_preview`  
Expected: local server starts on `http://127.0.0.1:8000/`

Run in another shell: `curl -sS http://127.0.0.1:8000/`  
Expected: returned HTML contains `UI Preview Workbench`.

- [ ] **Step 7: Commit the scenario layer**

```bash
git add tools/ui_preview/scenarios.js tools/ui_preview/index.html tools/ui_preview/app.js tools/ui_preview/styles.css tools/ui_preview/tests/check_preview.js
git commit -m "feat: add shared UI preview scenarios"
```

---

### Task 3: Implement the Web Console Preview

**Files:**
- Create: `tools/ui_preview/web_console.js`
- Modify: `tools/ui_preview/app.js`
- Modify: `tools/ui_preview/styles.css`
- Modify: `tools/ui_preview/tests/check_preview.js`

- [ ] **Step 1: Write failing assertions for web console rendering**

Extend `tools/ui_preview/tests/check_preview.js` to load the web renderer and assert that a `recording` scenario produces markup containing:

- `Status`
- `Sessions`
- `Tracks`
- `Settings`
- Recording CTA text
- At least one session filename
- At least one track name

Example:

```js
const webApi = require(path.join(root, "web_console.js"));
const recording = scenariosApi.getScenarioById("recording");
const markup = webApi.renderWebConsoleMarkup(recording);

assert.match(markup, /Status/);
assert.match(markup, /Sessions/);
assert.match(markup, /Tracks/);
assert.match(markup, /Settings/);
assert.match(markup, /Start Recording|Stop Recording/);
```

- [ ] **Step 2: Run the test and confirm it fails**

Run: `node tools/ui_preview/tests/check_preview.js`  
Expected: FAIL with missing module or missing renderer output.

- [ ] **Step 3: Implement the web console renderer**

Create `tools/ui_preview/web_console.js` with:

- `renderWebConsoleMarkup(scenario)`
- Small helpers for:
  - formatting best lap
  - rendering empty states for sessions/tracks
  - rendering button labels from `recording`

Keep the content structure aligned to `src/wifi_server.cpp`:

- Status card
- Sessions list
- Tracks list + action buttons
- Add Track form shell
- Settings form shell

Use a CommonJS/browser-compatible export pattern so the Node test and browser both use the same function.

- [ ] **Step 4: Wire the web preview into the app**

Update `app.js` so `renderWebConsoleMarkup(activeScenario)` is injected into `web-console-root` every time the active scenario changes.

- [ ] **Step 5: Add styling for web cards and form shells**

Update `styles.css` to give the web preview:

- A card layout
- Distinct headers
- Readable lists
- Clear action buttons
- Good spacing at desktop and mobile widths

Aim for “easy to inspect” rather than “final production brand”.

- [ ] **Step 6: Re-run the smoke test**

Run: `node tools/ui_preview/tests/check_preview.js`  
Expected: PASS with web console assertions succeeding.

- [ ] **Step 7: Commit the web console preview**

```bash
git add tools/ui_preview/web_console.js tools/ui_preview/app.js tools/ui_preview/styles.css tools/ui_preview/tests/check_preview.js
git commit -m "feat: add web console preview"
```

---

### Task 4: Implement the Device Screen Preview

**Files:**
- Create: `tools/ui_preview/device_screen.js`
- Modify: `tools/ui_preview/app.js`
- Modify: `tools/ui_preview/styles.css`
- Modify: `tools/ui_preview/tests/check_preview.js`

- [ ] **Step 1: Write failing assertions for device screen rendering**

Extend `tools/ui_preview/tests/check_preview.js` to load the device renderer and assert:

- Boot scenarios can render `GPS Searching...` and `READY`
- Driving scenarios can render `NO GPS`, `OFF TRACK`, `---`, or formatted delta values
- Session review scenarios can render a lap list view

Example:

```js
const deviceApi = require(path.join(root, "device_screen.js"));

assert.match(deviceApi.renderDeviceScreenMarkup(scenariosApi.getScenarioById("gps-searching")), /GPS Searching/);
assert.match(deviceApi.renderDeviceScreenMarkup(scenariosApi.getScenarioById("off-track")), /OFF TRACK/);
assert.match(deviceApi.renderDeviceScreenMarkup(scenariosApi.getScenarioById("session-review")), /Lap List|SESSION/);
```

- [ ] **Step 2: Run the test and confirm it fails**

Run: `node tools/ui_preview/tests/check_preview.js`  
Expected: FAIL because `device_screen.js` does not exist yet.

- [ ] **Step 3: Implement the device renderer**

Create `tools/ui_preview/device_screen.js` with:

- `renderDeviceScreenMarkup(scenario)`
- Internal helpers for:
  - boot state selection
  - delta formatting
  - lap time formatting
  - driving/status/lap-list markup blocks

Keep output aligned to `src/display.cpp`:

- boot screens: Splash, GPS Searching, Track Found, Recovery, Ready
- runtime screens: Driving, Status, Lap List
- driving screen states: no GPS, off-track, delta unavailable, valid delta

- [ ] **Step 4: Wire the device preview into the app**

Update `app.js` so `renderDeviceScreenMarkup(activeScenario)` is injected into `device-screen-root`.

- [ ] **Step 5: Style the device frame**

Update `styles.css` so the device preview has:

- A visible 320x240 screen area
- A surrounding frame to distinguish “device UI” from “browser UI”
- Monospace timing treatment
- Color-coded state backgrounds approximating the current TFT design

- [ ] **Step 6: Re-run the smoke test**

Run: `node tools/ui_preview/tests/check_preview.js`  
Expected: PASS with device rendering assertions succeeding.

- [ ] **Step 7: Commit the device screen preview**

```bash
git add tools/ui_preview/device_screen.js tools/ui_preview/app.js tools/ui_preview/styles.css tools/ui_preview/tests/check_preview.js
git commit -m "feat: add device screen preview"
```

---

### Task 5: Finish Scenario Coverage, Fallbacks, and First-Pass UI Polish

**Files:**
- Modify: `tools/ui_preview/scenarios.js`
- Modify: `tools/ui_preview/web_console.js`
- Modify: `tools/ui_preview/device_screen.js`
- Modify: `tools/ui_preview/styles.css`
- Modify: `tools/ui_preview/app.js`
- Modify: `tools/ui_preview/README.md`
- Modify: `tools/ui_preview/tests/check_preview.js`

- [ ] **Step 1: Add failing assertions for edge and fallback states**

Extend the smoke test to assert:

- Unknown scenario IDs fall back to `ready-to-drive`
- `heavy-track-library` renders multiple tracks/sessions without empty-state text
- `cold-boot`, `best-lap-improved`, and `session-review` are all present in the registry

Example:

```js
assert.equal(scenariosApi.getScenarioById("does-not-exist").id, "ready-to-drive");
assert.match(webApi.renderWebConsoleMarkup(scenariosApi.getScenarioById("heavy-track-library")), /track_/i);
```

- [ ] **Step 2: Run the smoke test and confirm it fails if any cases are missing**

Run: `node tools/ui_preview/tests/check_preview.js`  
Expected: FAIL until all required scenarios and fallback behavior are implemented.

- [ ] **Step 3: Complete the scenario set and shared data fidelity**

Update `scenarios.js` so all agreed scenarios exist with realistic sample data:

- empty sessions
- many sessions
- many tracks
- no GPS
- valid delta
- off-track
- lap list with enough rows to reveal scrolling/paging presentation

Ensure both preview renderers consume the same scenario source instead of duplicating hard-coded view data.

- [ ] **Step 4: Apply first-pass UI polish**

Update `styles.css`, `web_console.js`, and `device_screen.js` for the low-risk improvements already in scope:

- clearer spacing
- stronger section hierarchy
- more readable list density
- clearer button emphasis
- more legible device text sizing

Do not introduce new product features in this step.

- [ ] **Step 5: Update the README with the final run and verification flow**

Document:

- how to launch the preview
- where scenarios live
- which source files to check when UI diverges from firmware
- the manual acceptance checklist for web console and device screen coverage

- [ ] **Step 6: Run the smoke test again**

Run: `node tools/ui_preview/tests/check_preview.js`  
Expected: PASS with all scenario and fallback assertions succeeding.

- [ ] **Step 7: Run a manual preview smoke check**

Run: `python3 -m http.server 8000 --directory tools/ui_preview`  
Expected: server starts successfully.

Then open `http://127.0.0.1:8000/` and manually verify:

- scenario buttons switch both panels
- web console preview updates with the selected scenario
- device preview updates with the selected scenario
- the layout remains readable on a narrower browser width

- [ ] **Step 8: Commit the finished workbench**

```bash
git add tools/ui_preview/scenarios.js tools/ui_preview/web_console.js tools/ui_preview/device_screen.js tools/ui_preview/styles.css tools/ui_preview/app.js tools/ui_preview/README.md tools/ui_preview/tests/check_preview.js
git commit -m "feat: finish UI preview workbench"
```

---

## Final Verification Checklist

- [ ] Run: `node tools/ui_preview/tests/check_preview.js`
- [ ] Run: `python3 -m http.server 8000 --directory tools/ui_preview`
- [ ] Load: `http://127.0.0.1:8000/`
- [ ] Verify all 8 scenarios are selectable
- [ ] Verify `Web Console Preview` mirrors current dashboard sections
- [ ] Verify `Device Screen Preview` covers boot + driving + status + lap list states
- [ ] Verify unknown scenario IDs safely fall back to `ready-to-drive`

## Notes for Implementers

- Keep the preview static and framework-free.
- Treat `src/wifi_server.cpp` and `src/display.cpp` as the current product behavior source of truth.
- Prefer browser-safe, dependency-free code over clever abstractions.
- If renderer logic becomes hard to test, extract smaller pure helpers before adding more UI.
