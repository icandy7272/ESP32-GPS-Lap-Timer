# Web Mobile Navigation Refresh Design

**Date:** 2026-04-15  
**Status:** Approved for planning — amended 2026-04-15 after engineering review (see §4.4, §5.1.4–5, §5.2, §7)  
**Scope:** Mobile-first information architecture refresh for the ESP32-hosted web UI

## 1. Background

The current embedded web UI is functionally rich but structurally flat. The phone view renders `Status`, `Sessions`, `Tracks`, and `Settings` as one long page. That creates three related problems:

1. High-priority actions compete with low-priority settings for the same visual weight.
2. Phone users must scroll through unrelated sections to reach the task they actually came for.
3. The UI already hints at a product direction where `Sessions` is more important and `Settings` is less important, but the layout does not fully reflect that.

Existing project references already align on the four current feature areas:

- [docs/PRD.md](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/docs/PRD.md) defines the web surface as status, track management, session management, and settings.
- The archived UI preview design from commit `a190280` also grouped the web console into `Status / Sessions / Tracks / Settings`.
- The shipped code path in commit `45f7d4e` explicitly upgraded session presentation while demoting settings behind an `Advanced` affordance.

This design formalizes that product direction instead of leaving it implicit.

## 2. Goals

This refresh should:

1. Make the mobile web UI feel intentionally organized around the three most common jobs.
2. Preserve the existing firmware/API behavior while improving navigation and hierarchy.
3. Reduce the prominence of low-frequency settings without making them hard to find.
4. Keep track creation inside the track-management flow instead of creating a fragmented fourth task area.
5. Keep the desktop preview and the real firmware UI aligned on the same information architecture.

## 3. Non-Goals

This refresh does not:

1. Change API contracts or firmware business logic.
2. Introduce online maps, new track-creation capabilities, or session-analysis features.
3. Split track creation into its own tab.
4. Add a fourth primary destination for settings.
5. Rebuild the UI with a frontend framework.
6. Persist the active tab across page reloads. Cold load always lands on `Status`.
7. Introduce i18n infrastructure. Existing hardcoded copy (including Chinese strings in helper text) is preserved as-is. A separate spec will own i18n.
8. Replace the existing `/api/status` polling with SSE or WebSocket. Tab switching must not assume push semantics.

## 4. Approved IA

### 4.1 Primary Navigation

The mobile UI will use **three primary bottom tabs**:

1. `Status`
2. `Sessions`
3. `Tracks`

`Settings` is **not** a peer tab. It moves behind an `Advanced` affordance inside `Status`.

### 4.2 Default Destination

- Default tab on page load: `Status`
- The navigation should clearly indicate the active tab.
- The bottom tab bar should remain visible on mobile while avoiding overlap with the page content.

### 4.3 Content Ownership

#### Status

`Status` owns the operational summary and the primary action:

- GPS Fix
- Satellites
- Recording state
- Current Lap
- Best Lap
- Current Track
- recording CTA button
- recording-block reason / helper text
- `Advanced` entry point

`Advanced` expands within the `Status` tab and contains:

- Wi-Fi SSID
- Wi-Fi password
- brightness
- settings save action
- reboot-related helper copy

#### Sessions

`Sessions` owns only session-history tasks:

- session list
- metadata summary per session
- VBO download actions

It should feel like a dedicated archive/download destination, not a subsection buried below unrelated controls.

#### Tracks

`Tracks` owns all track-related tasks:

- current track summary
- nearby track chooser
- track list
- track actions
- full track-creation workflow

Track creation remains nested here because it is operationally part of track management, not a separate top-level mode.

### 4.4 State vs. View

Tab visibility must not change polling or state semantics. Specifically:

1. Shared runtime state (`_statusSnapshot`, `_isRec`, `_trackList`, the track-creation draft, and the in-memory session-list cache used for catch-up rendering, e.g. `_sessions`) continues to update regardless of which tab is active. Polling intervals do not pause on hidden tabs.
2. Renderers targeting any tab must be null-safe and side-effect-free when their target section is hidden. They may early-return on a `"is active tab"` check, but must not throw on missing DOM.
3. When the user switches tabs, a catch-up re-render must fire for the newly active tab so stale panels reflect the latest poll. This is an explicit event hook (`onActiveTabChange`), not a "render the world" call.
4. Track-creation widgets that depend on layout measurements (e.g. SVG review geometry) must re-run their layout step when `Tracks` becomes active, because hidden sections can produce zero-width measurements.

This section exists because the previous flat-page UI implicitly had every renderer write into a visible DOM every poll. The three-tab IA breaks that assumption and the contract above replaces it.

## 5. UX Rules

### 5.1 Mobile Rules

1. The bottom tab bar is mobile-first and always visible while navigating the main sections. It must be laid out with `position: fixed` on mobile (not `sticky`) so it stays anchored when the document body scrolls — `sticky` is unreliable inside a scrolling `body` on iOS Safari ≤ 15.
2. Content padding must account for the fixed bar so the last controls remain reachable. Use `padding-bottom: calc(88px + env(safe-area-inset-bottom))` on the app shell; the bar itself must also apply `env(safe-area-inset-bottom)` so iPhone home-indicator devices do not cover the tap targets.
3. Tabs should be text-first and concise. Icons are optional and not required for v1 of this refresh. Tap targets must be ≥ 44 px in both axes.
4. Switching tabs should be immediate and entirely client-side. No network request is triggered by a tab switch itself; the catch-up re-render in §4.4(3) uses already-polled state.
5. In landscape orientation (`@media (orientation: landscape) and (max-height: 500px)`), the bottom bar must either collapse to a thinner top-chip treatment or auto-hide until the next interaction, so it does not consume ~25% of the viewport on a phone in a windshield mount.
6. Cold load always lands on `Status` (per §3 Non-Goal #6).

### 5.2 Desktop Rules

Desktop and mobile share **one canonical DOM and one stylesheet**. The viewport breakpoint `@media (min-width: 720px)` is the only allowed mechanism for adapting presentation. Specifically:

- reuse the same section ownership, the same `section-*` markup, and the same active-tab model — no desktop-only markup branch, no parallel renderer in the firmware string builder
- at ≥ 720 px, the bottom tab bar may be re-positioned as a top or side nav via CSS only; the underlying DOM nodes and click handlers are unchanged
- content may breathe more on desktop via padding/max-width rules, but IA and section ownership are identical

This rule exists because the firmware and the preview share a single HTML string; any "desktop diverges from mobile" trade that widens that gap is rejected.

### 5.3 Settings Positioning

`Settings` is intentionally de-emphasized:

- still easy to discover from `Status`
- not promoted as a primary destination
- visually secondary compared to operational tasks

This follows the product decision implied by `feat: upgrade sessions and demote settings`.

## 6. Implementation Constraints

1. Keep the existing split firmware web UI structure:
   - [web_ui_markup.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui_markup.cpp)
   - [web_ui_script_dashboard.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui_script_dashboard.cpp)
   - [web_ui_script_track_creation.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui_script_track_creation.cpp)
2. Keep the preview workbench aligned with the firmware implementation:
   - [web_console.js](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/web_console.js)
   - [styles.css](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/styles.css)
3. Do not change route structure or API payloads for this refresh.
4. Prefer incremental UI-state additions over broad refactors.

## 7. Validation Criteria

The refresh is accepted when:

1. Mobile firmware UI exposes exactly three primary tabs: `Status`, `Sessions`, `Tracks`.
2. `Status` opens by default on cold load.
3. `Settings` is only reachable through `Advanced` inside `Status`. No `Settings` label appears as a primary tab button in either firmware or preview markup.
4. `Sessions` and `Tracks` panels render with no `Status` rows visible, and their content is reachable without scrolling past non-owned content.
5. The fixed tab bar does not cover important controls on narrow screens, and honors `env(safe-area-inset-bottom)` on iOS.
6. The preview workbench renders the same three `section-*` wrappers, the same `Advanced` placement inside `Status`, and asserts both in `tools/ui_preview/tests/check_preview.js`.
7. Existing firmware web UI regression tests still pass after being extended for tab behavior. Hidden-tab renderers are proven side-effect-free by a test that simulates a `/api/status` tick while the active tab is `Sessions`.
8. The refresh keeps one canonical firmware/preview IA shell: no duplicate desktop-only markup branch is introduced, and tab switching does not trigger new fetches outside the existing polling/on-demand data paths.
9. Active tab on cold load resets to `Status` (regardless of last-viewed tab) — per §3 Non-Goal #6.

## 8. Alternatives Considered

### Option A: 3 Tabs + Advanced

Approved.

Why it won:

- best balance of clarity and restraint
- matches current product direction
- keeps settings reachable but clearly secondary
- avoids overloading the bottom bar

### Option B: 4 Peer Tabs

Rejected for now.

Why:

- makes low-frequency settings too prominent
- leaves less room for future IA changes
- creates a busier mobile bottom bar without solving the hierarchy problem

### Option C: 2 Abstract Tabs

Rejected for this iteration.

Why:

- requires broader regrouping and copy changes
- adds avoidable implementation risk
- is too large a conceptual jump for this pass

## 9. Immediate Follow-Up

The next step is an implementation plan that:

1. adds regression coverage for tab state and content ownership
2. updates firmware markup/styles/runtime tab switching
3. aligns the preview workbench
4. updates product docs if needed once implementation lands

## 10. Post-Review Supplements (round 2, 2026-04-15)

A second engineering pass after the first revision surfaced three items worth pinning down in the spec so the plan does not have to re-litigate them.

### 10.1 Session-list cache is required, not incidental

This refresh treats the session-list cache as a requirement: the implementation **must** maintain an in-memory session cache (e.g. `_sessions`) populated by `loadSessions()` and consumed by the §4.4(3) catch-up hook. Without it, switching to `Sessions` would either show stale DOM or trigger a network fetch — both violate the "tab switch is client-side only" rule in §5.1(4). Cache bounds are out of scope for this refresh but see §10.3.

### 10.2 Size reporting is deferred follow-up, not a ship gate

The earlier draft proposed a hard cap of +1.5 KB gzipped on the served UI. That was dropped as speculative. A later revision tried to retain a lighter byte-reporting obligation, but the repository does not currently include a dedicated helper that reconstructs and measures the served UI from the real exported builders, so making this refresh block on byte reporting would force implementers to invent an ad hoc measurement path mid-flight.

Decision for this refresh: byte reporting is **not** a validation requirement and is **not** required in the final commit message. The only mandatory guardrails remain the executable checks in §7 and the implementation plan. If the team later decides size trending matters enough to enforce, add a dedicated measurement helper first and then reintroduce the requirement against the real exported builders (`build_web_ui_head_section()`, `build_web_ui_body_section()`, `build_web_ui_script_section()`), not against guessed function names.

### 10.3 Deferred UX / future-work items

Captured here so they don't get lost:

- **Deep-link to `Tracks` on reload when a track-creation draft is in progress.** Considered and dropped for this refresh — cold load always goes to `Status`. Revisit if field usage shows users frequently losing context mid-creation due to reboots or reloads.
- **`_sessions` cache upper bound.** The current firmware `/api/sessions` already paginates at the HTTP layer (TBD; verify during implementation). If pagination is absent, the cache will grow linearly with the number of VBO files on the SD card. Add a sanity cap (e.g. last 50 sessions) in a follow-up if this becomes measurable.
- **i18n** (already in §3 Non-Goals) — the hardcoded Chinese strings in `renderNearbyTrackChooser` and the Advanced helper text will eventually need an i18n pass. Tracked separately.
