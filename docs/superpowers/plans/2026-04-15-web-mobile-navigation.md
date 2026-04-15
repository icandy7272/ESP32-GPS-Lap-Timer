# Web Mobile Navigation Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorganize the ESP32-hosted web UI into three mobile-first primary tabs (`Status`, `Sessions`, `Tracks`) with `Settings` demoted into `Advanced` inside `Status`, while keeping firmware and preview behavior aligned.

**Architecture:** Keep the existing embedded HTML/CSS/JS split and add a small client-side tab state on top of it. Re-scope section ownership without changing API contracts, then mirror the same IA in the preview workbench and regression suites.

**Tech Stack:** Embedded HTML/CSS/JavaScript in `src/wifi/`, vanilla JS preview renderer in `tools/ui_preview/`, Node `assert` regression tests.

---

## Pre-Flight

Before starting Task 1, **commit or stash the in-flight session word-break changes** currently in the working tree:

- `src/wifi/web_ui_markup.cpp` (session metadata + link word-break CSS)
- `tools/ui_preview/styles.css` (matching preview CSS)
- `tools/ui_preview/tests/check_preview.js` (assertions for the above)

These three files are also modified by this plan. They are orthogonal to the tab-navigation refresh (confirmed during engineering review), but proceeding with them dirty guarantees merge friction and risks accidental revert. A one-line commit such as `fix: word-break overflow in session cards` is sufficient.

## Contract Notes From Engineering Review

Two facts from the existing test harness drive several choices below; plan authors should not re-litigate them:

1. The fake DOM element at [tools/ui_preview/tests/check_firmware_web_ui.js:164-189](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/tests/check_firmware_web_ui.js) has a mutable `style: {}` object, so `.style.display` writes are observable in tests. For the new tab sections, still prefer class-based visibility because the shipped UI wants `active` classes on `section-*` wrappers; inline `style.display` remains acceptable for existing affordances such as the `Advanced` panel.
2. The harness runs the embedded script with `vm.runInNewContext`, so top-level `var` and `function` declarations in `web_ui_script_dashboard.cpp` become properties on `harness.context`. This is how the existing `toggleAdvancedSettings` is exposed, and how the new `setActiveTab` will be exposed automatically.

### Visibility Control — Two-Mode Rule

The plan now relies on two distinct visibility mechanisms. Keeping them straight prevents test / implementation drift:

| Element | Control | Rendered by | Test assertion |
|---|---|---|---|
| `section-status` / `section-sessions` / `section-tracks` | `className` containing `active` | `renderPrimaryTabs()` | `hasClass(el, "active")` |
| `advanced-settings-panel` | inline `style.display` (`"block"` / `"none"`) | `renderAdvancedSettings()` ([web_ui_script_dashboard.cpp:242](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui_script_dashboard.cpp)) | `el.style.display === "block"` OR `harness.context._advancedSettingsOpen` |

Do not mix: implementations must not use inline `style.display` on `section-*` wrappers (it would bypass the CSS cascade for the `@media (min-width: 720px)` desktop override), and tests must not assert class membership on `advanced-settings-panel` (that element carries its own `.advanced-panel` class whose presence is orthogonal to visibility).

### Sessions Cache Obligations

Task 3 Step 2 introduces `var _sessions = []` and splits `loadSessions()` into a fetcher + a pure `renderSessionsList()` renderer. Two obligations that flow from spec §10.1 and §10.3:

- The cache is **required**, not decorative — without it, `onActiveTabChange('sessions')` would have no data source and the "tab switch is client-side only" rule (spec §5.1(4)) would be violated on first switch.
- `_sessions` has no upper bound in this plan. Verify during implementation whether `/api/sessions` paginates; if it returns the full list, add a comment in the code noting the unbounded-growth risk and raise a follow-up before a user with hundreds of VBOs hits memory pressure. Do not add a cap in this refresh — out of scope.

---

## File Map

### Firmware files

- `src/wifi/web_ui_markup.cpp`
  Owns the HTML/CSS shell. This plan updates the section wrappers, tab bar shell, mobile spacing, and `Advanced` placement.
- `src/wifi/web_ui_script_dashboard.cpp`
  Owns runtime behavior for status/sessions/tracks/settings. This plan adds tab state, section visibility control, and `Advanced` behavior inside `Status`.
- `src/wifi/web_ui_script_track_creation.cpp`
  Must remain inside the `Tracks` destination. Touch only if the new section wrappers require track-creation mount-point adjustments.

### Preview files

- `tools/ui_preview/web_console.js`
  Must render the same three-tab IA and content ownership as the firmware UI.
- `tools/ui_preview/styles.css`
  Owns preview layout and responsive behavior. This plan adds the preview tab shell and mobile fixed-bar treatment.

### Test files

- `tools/ui_preview/tests/check_firmware_web_ui.js`
  Extend runtime expectations for tab defaults, switching, and `Advanced` visibility.
- `tools/ui_preview/tests/check_preview.js`
  Extend static preview expectations for the new IA and shell.

### Docs

- `docs/PRD.md`
  Optional follow-up once implementation lands, to reflect the new web IA in the product doc.

---

### Task 1: Lock The New IA In Regression Tests

**Files:**
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/tests/check_firmware_web_ui.js`
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/tests/check_preview.js`

- [ ] **Step 1: Write the failing firmware runtime test for tab defaults**

Add a test that expects:

- `Status` is active on first load
- `Sessions` and `Tracks` sections start hidden
- the bottom nav exposes exactly `Status`, `Sessions`, `Tracks`
- `Settings` fields live inside the `Status` destination rather than a peer section

Example test shape:

```js
async function testDashboardUsesThreePrimaryTabs() {
  const harness = createHarness();
  await harness.settle();

  function hasClass(el, name) {
    return (el.className || "").split(" ").indexOf(name) >= 0;
  }

  assert.equal(harness.getElement("tab-status").getAttribute("data-active"), "true");
  assert.equal(harness.getElement("tab-sessions").getAttribute("data-active"), "false");
  assert.equal(harness.getElement("tab-tracks").getAttribute("data-active"), "false");
  assert.ok(hasClass(harness.getElement("section-status"), "active"));
  assert.ok(!hasClass(harness.getElement("section-sessions"), "active"));
  assert.ok(!hasClass(harness.getElement("section-tracks"), "active"));
}
```

**Why use class checks here:** for the primary tab sections, class membership is the right assertion because the shipped UI should toggle `active` on `section-*` wrappers rather than rewriting inline display styles. The harness can still observe `.style.display`, so existing tests for controls like `advanced-settings-panel` may continue to use it. `data-active` is a real attribute round-trippable through the fake's `setAttribute`/`getAttribute`, so that one stays.

- [ ] **Step 2: Write the failing firmware runtime test for tab switching and Advanced**

Add a second test that:

- switches to `Sessions` and confirms only session content is visible
- switches to `Tracks` and confirms track creation still lives there
- returns to `Status`, opens `Advanced`, and confirms settings fields are visible there

Example:

```js
harness.context.setActiveTab("sessions");
assert.ok(hasClass(harness.getElement("section-sessions"), "active"));
assert.ok(!hasClass(harness.getElement("section-status"), "active"));

harness.context.setActiveTab("status");
harness.context.toggleAdvancedSettings();
assert.equal(harness.context._advancedSettingsOpen, true);
assert.equal(harness.getElement("advanced-settings-panel").style.display, "block");

// Tab switch while Advanced is open MUST force-close it (§4.4 / Task 3 Step 5).
harness.context.setActiveTab("tracks");
assert.equal(harness.context._advancedSettingsOpen, false);
```

- [ ] **Step 3: Write the failing preview smoke assertions**

Extend `check_preview.js` to require:

- three tab labels in the preview markup
- `Advanced` copy inside the `Status` card shell
- no fourth primary `Settings` tab

Example assertions:

```js
assert.ok(markup.indexOf("Status") >= 0);
assert.ok(markup.indexOf("Sessions") >= 0);
assert.ok(markup.indexOf("Tracks") >= 0);
assert.ok(markup.indexOf("Advanced") >= 0);
// A primary tab button labelled "Settings" must not exist. The substring check
// below is robust against attribute order and whitespace; a regex with
// /<button[^>]*>Settings<\/button>/ false-negatives on newlines and false-
// positives on "Advanced Settings".
assert.ok(markup.indexOf(">Settings<") < 0, "Settings must not appear as a primary tab label");
```

- [ ] **Step 4: Run the tests to verify they fail**

Run:

```bash
node tools/ui_preview/tests/check_firmware_web_ui.js
node tools/ui_preview/tests/check_preview.js
```

Expected:

- `check_firmware_web_ui.js` fails because no tab state exists yet
- `check_preview.js` fails because the preview still renders the old flat-card structure

- [ ] **Step 5: Commit**

```bash
git add tools/ui_preview/tests/check_firmware_web_ui.js tools/ui_preview/tests/check_preview.js
git commit -m "test: lock web mobile tab navigation expectations"
```

### Task 2: Add The Firmware Tab Shell And Mobile Layout

**Files:**
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui_markup.cpp`

- [ ] **Step 1: Add the failing markup-level expectations if Task 1 did not already cover them**

If needed, extend the tests so they explicitly require:

- section wrappers with stable IDs like `section-status`, `section-sessions`, `section-tracks`
- bottom nav buttons with stable IDs like `tab-status`, `tab-sessions`, `tab-tracks`
- bottom safe-area padding for the main content region

- [ ] **Step 2: Add the tab-aware body structure**

Refactor the body markup so the long page becomes:

```html
<div id="app-shell" class="app-shell">
  <div id="section-status" class="tab-section active">...</div>
  <div id="section-sessions" class="tab-section">...</div>
  <div id="section-tracks" class="tab-section">...</div>
  <nav id="bottom-tabs" class="bottom-tabs">
    <button id="tab-status" class="tab-btn tab-btn--active" data-active="true">Status</button>
    <button id="tab-sessions" class="tab-btn" data-active="false">Sessions</button>
    <button id="tab-tracks" class="tab-btn" data-active="false">Tracks</button>
  </nav>
</div>
```

Critical: `section-status` carries `class="tab-section active"` and `tab-status` carries `class="tab-btn tab-btn--active" data-active="true"` **in the server-rendered HTML**. This is not cosmetic — it makes the Task 2 commit immediately usable on its own, eliminates the cold-load flash in §4.4, and lets the Task 1 default-state tests pass without waiting for JS init.

Move the existing cards into the correct section wrappers:

- `status-card` and `advanced-settings-panel` into `section-status`
- `sessions` card into `section-sessions`
- current track, nearby tracks, track list, and track creation into `section-tracks`

`#rec-cta-reason` stays directly under the recording CTA inside `section-status` — do not separate it.

- [ ] **Step 3: Add minimal mobile-first CSS**

Inside the embedded style builder, add:

- a shell container with bottom padding that accounts for the fixed nav + iOS home indicator
- `.tab-section` visibility rules driven by the `active` class (not inline style)
- `.bottom-tabs` **`position: fixed`** mobile treatment with safe-area inset
- active/inactive tab button styles
- a single `@media (min-width: 720px)` breakpoint (per spec §5.2) that re-presents the bar without changing the DOM or the IA
- a landscape guard (`@media (orientation: landscape) and (max-height: 500px)`) per spec §5.1.5

Target pattern (illustrative — inline style builder syntax applies):

```css
.app-shell{padding-bottom:calc(88px + env(safe-area-inset-bottom))}
.tab-section{display:none}
.tab-section.active{display:block}
.bottom-tabs{
  position:fixed;
  left:0;right:0;bottom:0;
  display:grid;grid-template-columns:repeat(3,1fr);
  padding-bottom:env(safe-area-inset-bottom);
  background:#fff;
  border-top:1px solid #e5e5e5;
}
.tab-btn{min-height:44px}
.tab-btn--active{font-weight:600}
@media (orientation: landscape) and (max-height: 500px){
  .bottom-tabs{position:static;padding:4px 0}
  .app-shell{padding-bottom:0}
}
@media (min-width: 720px){
  .bottom-tabs{position:sticky;bottom:auto;top:0;border-top:none;border-bottom:1px solid #e5e5e5}
  .app-shell{padding-bottom:0;padding-top:8px}
}
```

Do not use `position: sticky` as the mobile default — `sticky` on a child of a scrolling `body` is unreliable on iOS Safari ≤ 15. The breakpoint can opt into sticky for desktop since the layout there is wider and not scroll-trapped.

- [ ] **Step 4: Keep `Settings` secondary**

Retain `Advanced` as a secondary control within the `Status` section. Do not reintroduce `Settings` as a fourth peer destination anywhere in the body markup.

- [ ] **Step 5: Run the firmware runtime test**

Run:

```bash
node tools/ui_preview/tests/check_firmware_web_ui.js
```

Expected:

- tab structure assertions may still fail on behavior
- markup presence assertions should now pass

- [ ] **Step 6: Commit**

```bash
git add src/wifi/web_ui_markup.cpp
git commit -m "feat: add mobile tab shell to firmware web ui"
```

### Task 3: Add Firmware Tab State And Section Switching

**Files:**
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui_script_dashboard.cpp`
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui_script_track_creation.cpp`

- [ ] **Step 1: Add the failing behavior test if any runtime state is still uncovered**

Before implementation, make sure the test suite explicitly checks:

- `setActiveTab("sessions")` updates button state
- inactive sections are hidden
- returning to `tracks` keeps track-creation controls reachable
- `Advanced` can only be opened from `Status`

- [ ] **Step 2: Add a tiny tab state model**

In `web_ui_script_dashboard.cpp`, add:

```js
var _activeTab = "status";
var _sessions = [];

function setActiveTab(tab) {
  if (_activeTab === tab) { return; }
  var prev = _activeTab;
  _activeTab = tab;
  // Spec §5.3 / Task 3 Step 5: tab switches always close Advanced.
  if (prev !== tab) {
    _advancedSettingsOpen = false;
    renderAdvancedSettings();
  }
  renderPrimaryTabs();
  onActiveTabChange(tab);
}
```

Refactor the existing sessions fetch path into two functions:

```js
function renderSessionsList() {
  var ul = $('sessions');
  if (!ul) { return; }
  ul.innerHTML = '';
  if (!_sessions.length) {
    ul.innerHTML = '<div class="helper-text">No sessions</div>';
    return;
  }
  _sessions.forEach(function(rawSession) {
    var session = normalizeSessionItem(rawSession);
    // existing card rendering body
  });
}

function loadSessions() {
  fetch('/api/sessions').then(function(r){return r.json();}).then(function(d){
    _sessions = d.sessions || [];
    renderSessionsList();
  }).catch(function(){
    _sessions = [];
    renderSessionsList();
  });
}
```

Then implement `renderPrimaryTabs()` to:

- add the `active` class to the target `section-*` wrapper and remove it from the other two (via `el.className = ...`, not `el.style.display = ...`)
- set `data-active="true"` on the active `tab-*` button (via `setAttribute`) and `"false"` on the others; also toggle the `tab-btn--active` class for styling
- keep DOM updates localized and predictable — no `innerHTML` rewrites of section bodies here

`onActiveTabChange(tab)` is a small hook implementing spec §4.4(3): when a tab becomes active, run the catch-up renderer for that tab against the already-cached `_statusSnapshot`, `_sessions`, and `_trackList` so the panel is never stale. Introducing `_sessions` plus `renderSessionsList()` in this step is what makes the sessions catch-up path real instead of aspirational. See new Step 5 below for its body.

- [ ] **Step 3: Wire tab click handlers during startup**

On initialization, bind:

```js
if ($("tab-status")) { $("tab-status").onclick = function(){ setActiveTab("status"); }; }
if ($("tab-sessions")) { $("tab-sessions").onclick = function(){ setActiveTab("sessions"); }; }
if ($("tab-tracks")) { $("tab-tracks").onclick = function(){ setActiveTab("tracks"); }; }
```

Call `renderPrimaryTabs()` during initial boot so the default state is visible before the first data refresh completes.

- [ ] **Step 4: Preserve track creation inside `Tracks`**

`web_ui_script_track_creation.cpp` accesses DOM only via `$('creation-stage-name')` / `$('creation-step-start-finish')` / etc. — pure global-id lookups, not parent-scoped queries. Wrapping these elements inside `section-tracks` does not break the lookups themselves.

The real risks, and the specific guards to add:

1. **`renderTrackDraft()` runs every 2 s from `refreshStatus()`** even when `section-tracks` is hidden. Add a fast-path guard:
   ```js
   function renderTrackDraft(){
     if (_activeTab !== 'tracks') { return; }
     // existing body
   }
   ```
   This preserves spec §4.4(1) — state keeps updating, but rendering side-effects only happen when visible. The per-tick work drops to a single integer compare when not on Tracks.

2. **SVG review geometry uses layout measurements** (`getBoundingClientRect` / computed width). First paint of `section-tracks` while hidden (`display: none`) yields a zero client width, so `renderTrackDraft()` / review-panel layout produces a zero-sized SVG. This is fixed by the new Step 5 hook (catch-up re-render on tab activation).

3. Only touch `web_ui_script_track_creation.cpp` to add the guard above. No other edits to that file are expected — if the reviewer finds a second selector that broke, flag it rather than silently patch.

- [ ] **Step 5: Implement `onActiveTabChange` catch-up hook (spec §4.4(3))**

Inside `web_ui_script_dashboard.cpp`:

```js
function onActiveTabChange(tab) {
  if (tab === 'status') {
    renderCurrentTrackSummary();
    updateRecBtn();
  } else if (tab === 'sessions') {
    // Sessions list is normally refreshed on demand. A catch-up render is
    // cheap because Task 3 Step 2 introduced the in-memory _sessions cache.
    renderSessionsList();
  } else if (tab === 'tracks') {
    renderCurrentTrackSummary();
    renderNearbyTrackChooser();
    renderTracksList();
    // Force a re-measurement pass now that section-tracks has non-zero width.
    renderTrackDraft();
  }
}
```

Rules:

- The hook reads from cached state (`_statusSnapshot`, `_sessions`, `_trackList`, `_trackDraft`). It does NOT issue new fetches — the 2 s polling loop plus explicit `loadSessions()` / `loadTracks()` refreshes remain authoritative for data freshness.
- If a renderer is already gated by `if (_activeTab !== 'xxx') return;` (per Step 4 guard on `renderTrackDraft`), call it here after the tab has switched so the guard passes.
- Keep this function small. Anything more than a dispatch table suggests a renderer has hidden coupling that should be factored out.

Extend the Task 1 Step 2 test to cover the catch-up behavior:

```js
// Pre-seed status with a track while on Status (hidden Tracks won't render).
await harness.respondTo('/api/status', { ..., track: 'Circuit A', current_track_distance_m: 12.3 });
await harness.tickStatus();

// Switch to Tracks — catch-up must populate the current-track summary.
harness.context.setActiveTab('tracks');
assert.ok(harness.getElement('current-track-name').textContent.indexOf('Circuit A') >= 0);
```

- [ ] **Step 6: Force-close `Advanced` on tab switch (spec §5.3)**

The rule: switching away from `Status` unconditionally closes the Advanced panel. Switching back to `Status` shows the panel closed. This gives a predictable mental model and lets the test assert a single boolean.

Implementation note: **do NOT** call `toggleAdvancedSettings()` from `setActiveTab()` — that function is a pure toggle ([web_ui_script_dashboard.cpp:323](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui_script_dashboard.cpp)), so calling it when the panel is already closed would re-open it. The Step 2 snippet handles this correctly by assigning `_advancedSettingsOpen = false` directly and then calling `renderAdvancedSettings()`. Preserve that pattern; do not introduce a `toggleAdvancedSettings(false)` overload.

Test assertion (already included in Task 1 Step 2 example): after `setActiveTab('tracks')`, `harness.context._advancedSettingsOpen === false`.

- [ ] **Step 7: Run the full firmware UI regression suite**

Run:

```bash
node tools/ui_preview/tests/check_firmware_web_ui.js
```

Expected:

`check_firmware_web_ui: PASS`

- [ ] **Step 8: Commit**

```bash
git add src/wifi/web_ui_script_dashboard.cpp src/wifi/web_ui_script_track_creation.cpp tools/ui_preview/tests/check_firmware_web_ui.js
git commit -m "feat: add firmware web tab switching"
```

### Task 4: Mirror The Same IA In The Preview Workbench

**Files:**
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/web_console.js`
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/styles.css`
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/tests/check_preview.js`

- [ ] **Step 1: Update the preview renderer structure**

Current `renderWebConsoleMarkup()` in [tools/ui_preview/web_console.js](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/web_console.js) emits **four** cards:

- `data-card="status"`
- `data-card="sessions"`
- `data-card="tracks"`
- `data-card="settings"` (contains a disabled `Advanced` placeholder and a Wi-Fi reboot hint)

Refactor so it emits:

- one shared tab bar (`web-console__tabs`)
- one status panel with the recording CTA and `Advanced` entry point
- one sessions panel
- one tracks panel
- **no `data-card="settings"` card.** That card is deleted; its copy (the Wi-Fi reboot hint) moves inside the `Advanced` area of the status panel.

Recommended output shape:

```js
return (
  '<div class="web-console-preview">' +
    '<div class="web-console__tabs">' +
      '<button class="web-console__tab web-console__tab--active" data-tab="status" data-active="true">Status</button>' +
      '<button class="web-console__tab" data-tab="sessions" data-active="false">Sessions</button>' +
      '<button class="web-console__tab" data-tab="tracks" data-active="false">Tracks</button>' +
    '</div>' +
    '<div class="web-console__panel web-console__panel--status active" data-panel="status">...</div>' +
    '<div class="web-console__panel web-console__panel--sessions" data-panel="sessions">...</div>' +
    '<div class="web-console__panel web-console__panel--tracks" data-panel="tracks">...</div>' +
  '</div>'
);
```

Desktop vs mobile presentation (per spec §5.2 — one DOM, one stylesheet):

- The preview renders a single DOM containing all three panels. CSS at viewport width ≥ 720 px flattens the stack so all three are visible side-by-side for design review (existing behavior, roughly).
- Below 720 px, the `active` class on panels controls visibility, matching the firmware behavior exactly.
- The preview does NOT need to bind tab-click handlers for functional switching (it is a static review surface), but the IA, section IDs, and `Advanced`-inside-Status placement must match the firmware.

- [ ] **Step 2: Update preview styles**

Add preview classes for:

- tab shell
- active tab visual treatment
- fixed mobile bottom nav
- per-panel spacing and padding compensation

Keep the existing visual language instead of redesigning the entire theme.

- [ ] **Step 3: Tighten preview assertions**

Make `check_preview.js` assert:

- `Advanced` exists inside the status destination
- `Tracks` contains current track, nearby tracks, and track creation
- `Sessions` remains a focused archive/download area

- [ ] **Step 4: Run the preview test**

Run:

```bash
node tools/ui_preview/tests/check_preview.js
```

Expected:

`preview scaffold ok`

- [ ] **Step 5: Commit**

```bash
git add tools/ui_preview/web_console.js tools/ui_preview/styles.css tools/ui_preview/tests/check_preview.js
git commit -m "feat: align preview workbench with mobile web tabs"
```

### Task 5: Final Verification And Doc Sync

**Files:**
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/docs/PRD.md`

- [ ] **Step 1: Update the PRD web-page summary**

Change the web-page section in `docs/PRD.md` so it reflects the shipped IA:

- three primary tabs: `Status`, `Sessions`, `Tracks`
- `Settings` moved under `Advanced` inside `Status`

- [ ] **Step 2: Run both regression suites back-to-back**

Run:

```bash
node tools/ui_preview/tests/check_firmware_web_ui.js
node tools/ui_preview/tests/check_preview.js
```

Expected:

- `check_firmware_web_ui: PASS`
- `preview scaffold ok`

- [ ] **Step 3: Hidden-tab side-effect regression**

Spec §7(7) requires proof that renderers are side-effect-free when their target tab is hidden. Add one test in `check_firmware_web_ui.js` that:

1. Starts in `Status`.
2. Enqueues a `/api/status` payload that would normally trigger `renderNearbyTrackChooser` / `renderTracksList` writes.
3. Asserts that the `section-tracks` DOM nodes (e.g. `tracks-list`, `current-track-name`) have NOT been written by the tick (check `innerHTML` / `textContent` is unchanged).
4. Switches to `Tracks` and asserts the catch-up hook populates them.

This locks in §4.4(2) — hidden-tab renderers no-op — and prevents a regression where a future editor re-enables the unconditional writes.

- [ ] **Step 4: Architecture guardrail check (spec §7(8))**

Confirm the refresh stayed within the intended structural constraints:

```bash
rg -n "section-status|section-sessions|section-tracks|bottom-tabs" src/wifi/web_ui_markup.cpp
rg -n "fetch\\(" src/wifi/web_ui_script_dashboard.cpp src/wifi/web_ui_script_track_creation.cpp
```

Expected evidence:

- one canonical set of `section-*` wrappers and one `bottom-tabs` shell in the firmware markup builder
- no new `fetch()` introduced inside `setActiveTab()` / `onActiveTabChange()`; tab switching only re-renders from cached state
- existing on-demand data loaders (`loadSessions()`, `loadTracks()`, `/api/status` polling) remain the only network entry points for freshness

Do not add ad hoc byte-measurement commands here. Spec §10.2 now explicitly defers size reporting until the repo has a dedicated helper that measures the real exported builders. For this refresh, the architecture guardrail evidence above is the complete requirement.

- [ ] **Step 5: Manual narrow-screen pass in the preview**

Run:

```bash
python3 -m http.server 8000 --bind 127.0.0.1 --directory tools/ui_preview
```

Manual checklist:

- `Status` opens first (cold load), even after previously viewing `Tracks` and reloading — spec §7(9)
- fixed bottom nav stays anchored while the body scrolls on a narrow viewport
- last controls are not covered by the bar (both on a regular screen and an iPhone-with-home-indicator mock)
- rotating to landscape collapses / hides the bar per spec §5.1.5
- `Advanced` is visible only inside `Status`, and closes when switching tabs
- `Sessions` feels dedicated to downloads/history
- `Tracks` owns current track + nearby + creation flow, with no zero-width SVG if the creation flow was entered mid-session from another tab

- [ ] **Step 6: Commit**

```bash
git add docs/PRD.md tools/ui_preview/tests/check_firmware_web_ui.js
git commit -m "docs: update web ia for mobile tab navigation"
```

Include the architecture-guardrail evidence from Step 4 in the commit body.
