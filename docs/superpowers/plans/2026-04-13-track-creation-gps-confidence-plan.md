# Track Creation GPS Confidence Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make start/finish creation feel trustworthy before the kart goes on track by replacing one-shot point capture with confidence-aware sampled marking, clearer offline review, and better save diagnostics.

**Architecture:** Keep the experience fully offline inside the existing ESP32-hosted web UI. Reuse the current `/api/status` snapshot flow, but add richer client-side point quality logic, multi-step review gates, and stronger firmware-side save diagnostics so the user can tell whether a line is trustworthy before creating the track.

**Tech Stack:** Embedded HTML/JS in `src/wifi/web_ui.cpp`, existing `/api/status` and `/api/tracks` handlers, host-side JS regression tests, portable pure-logic host tests where extraction makes sense.

---

## Current-State Notes

- The web UI currently allows marking when the last three GPS samples are present, each sample comes from `/api/status`, satellite count is at least `4`, and max pairwise spread is at most `2.5 m`.
- The visible coordinate readout is refreshed on a `2000 ms` polling interval in [src/wifi/web_ui.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui.cpp#L1331).
- Actual marking does **not** use the last painted value directly. `markStartFinishPoint()` calls `captureCurrentGpsPoint()`, which forces a fresh `/api/status` request immediately before storing the point in [src/wifi/web_ui.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui.cpp#L818) and [src/wifi/web_ui.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui.cpp#L952).
- Firmware-side GPS state is written into `session_state` from `lap_timer_task` after each received fix in [src/lap_timer/lap_timer_delta.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/lap_timer/lap_timer_delta.cpp#L8). At the default `25 Hz` GPS rate in [src/gps.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/gps.cpp#L11), the additional firmware-side staleness should usually be on the order of one fix interval plus HTTP/UI overhead, while the painted coordinate text can be as old as nearly two seconds between polls.
- This mismatch between the painted coordinate age and the actual mark-time fetch is currently invisible to the user. The UI does not show when the displayed coordinate was last refreshed, whether it is stale, or whether a mark operation is using a fresher sample than the one on screen.
- The current parser only handles GGA and RMC sentences. HDOP is **not** available today, so v1 confidence must be based on satellite count, observed sample spread, and sample freshness unless new NMEA parsing is added first.
- The review panel is currently a local SVG geometry sketch, not an internet map. It works offline but distorts perception because the current rendering logic stretches lat and lon independently to fill the panel in [src/wifi/web_ui.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui.cpp#L694).
- Both `markStartFinishPoint()` and `markSectorPoint()` currently rely on the shared `captureCurrentGpsPoint()` helper. Any sampled-capture change must therefore apply to **all** point-capture paths, not just the start/finish wizard buttons.

## Product Direction

- Preserve the offline-only workflow. Do not require internet map tiles.
- Treat `2-3 m` lines as a high-risk input that needs stricter confidence checks.
- Separate three concepts in the UI:
  - GPS readiness to attempt a measurement
  - confidence of a captured point
  - confidence of the finished line
- Make coordinate freshness explicit. The user should be able to tell whether the visible coordinate is live, slightly stale, or freshly sampled for a mark action.
- Lock v1 to a concrete sampling policy so implementers do not make product decisions mid-task: a fixed sample window, minimum valid sample count, one aggregation method, and explicit failure behavior.
- Choose explicit v1 defaults inside the plan when behavior affects whether the user is blocked. Do not leave shipped defaults to implementation-time judgment.
- Whenever a task says to run the UI tests, run the full `check_firmware_web_ui.js` suite, not only the newly added cases.
- Prefer transparent diagnostics over opaque pass/fail states.

### Task 1: Define The Confidence Model

**Files:**
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui.cpp`
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/web_console.js`
- Test: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/tests/check_firmware_web_ui.js`

- [ ] **Step 1: Write the failing UI tests for richer GPS readiness states**

Add tests that expect distinct readiness labels and blocking reasons for:
- fewer than the required satellites
- enough satellites but not enough samples yet
- sampled spread too large
- short-line strict mode requesting higher confidence

- [ ] **Step 2: Run the targeted UI tests to verify they fail**

Run: `node tools/ui_preview/tests/check_firmware_web_ui.js`

Expected: assertions fail because the UI only exposes the current binary-ready behavior.

- [ ] **Step 3: Define a small client-side quality model**

Introduce a pure helper layer in the embedded script for:
- point sampling state
- point spread radius
- line length
- line risk tier
- overall line confidence (`low`, `medium`, `high`)

Keep v1 client-side for quality scoring. Document HDOP as a future upgrade path that requires new GSA parsing, new session state plumbing, and `/api/status` exposure before it can influence confidence.

- [ ] **Step 4: Implement minimal UI copy for the new states**

Add user-facing labels such as:
- `Need better GPS`
- `Sampling point...`
- `Noisy - try again`
- `Acceptable - short lines may drift`
- `Good - ready to save`
- `Short line: higher confidence required`

- [ ] **Step 5: Run the targeted UI tests and make them pass**

Run: `node tools/ui_preview/tests/check_firmware_web_ui.js`

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/wifi/web_ui.cpp tools/ui_preview/web_console.js tools/ui_preview/tests/check_firmware_web_ui.js
git commit -m "feat: add gps confidence states for track creation"
```

### Task 2: Improve Coordinate Freshness Feedback

**Files:**
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui.cpp`
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/web_console.js`
- Test: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/tests/check_firmware_web_ui.js`

- [ ] **Step 1: Write failing tests for coordinate freshness UX**

Cover:
- visible coordinate text includes freshness metadata or a stale/live badge
- track-creation mode polls more frequently than the default status screen
- stale coordinates are visually downgraded instead of looking equally trustworthy
- slow poll responses do not cause overlapping queued requests
- freshness copy uses coarse buckets instead of sub-second flickering text

- [ ] **Step 2: Run the targeted UI test and verify it fails**

Run: `node tools/ui_preview/tests/check_firmware_web_ui.js`

Expected: FAIL because the current UI only shows raw coordinates with no age or freshness state.

- [ ] **Step 3: Implement minimal adaptive polling**

Inside `src/wifi/web_ui.cpp`:
- keep the default low-frequency poll outside track creation
- increase poll cadence while the creation UI is open
- never poll faster than `300 ms`
- if one `/api/status` request is still in flight, skip the next tick instead of queueing another
- limit the adaptive cadence to the track-creation flow, not the main dashboard

Prefer a simple timer-based approach over SSE/WebSockets for the first version so the behavior stays easy to reason about on ESP32.

- [ ] **Step 4: Surface freshness in the UI**

Add user-facing elements such as:
- `Live`
- `1s ago`
- `2s ago`
- `Stale - refresh before trusting`

Update the age label at most once per second so fast polling does not create visible flicker.

- [ ] **Step 5: Run the targeted UI tests and make them pass**

Run: `node tools/ui_preview/tests/check_firmware_web_ui.js`

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/wifi/web_ui.cpp tools/ui_preview/web_console.js tools/ui_preview/tests/check_firmware_web_ui.js
git commit -m "feat: expose gps coordinate freshness in track creation"
```

### Task 3: Replace One-Shot Marking With Timed Point Sampling

**Files:**
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui.cpp`
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/web_console.js`
- Test: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/tests/check_firmware_web_ui.js`

- [ ] **Step 1: Write failing tests for sampled point capture**

Cover:
- pressing `Mark P1` starts a timed sampling state instead of capturing immediately
- active sampling temporarily increases refresh cadence again
- multiple fresh `/api/status` samples are collected during a fixed `2400 ms` sample window at `400 ms` cadence
- the stored point becomes the component-wise median of the valid sample set in local meter space, not the first sample
- fewer than `3` valid samples causes capture failure and no point is stored
- GPS loss during the window causes capture failure and a retry prompt
- the user can cancel the sampling window without storing a point
- rapid `P1` then `P2` taps while sampling is active are rejected instead of queued
- sector point marking also uses the sampled-capture path end-to-end
- the UI displays progress and quality information after capture

- [ ] **Step 2: Run the targeted test and verify it fails**

Run: `node tools/ui_preview/tests/check_firmware_web_ui.js`

Expected: FAIL because marking currently stores a single fresh point.

- [ ] **Step 3: Implement minimal sampled capture flow**

Inside `src/wifi/web_ui.cpp`:
- add a per-point sampling session model
- apply that sampling flow at the shared point-capture layer so start/finish and sector marks inherit the same behavior
- request several fresh status samples over a fixed `2400 ms` window on a `400 ms` cadence with at most one in-flight request
- fail the capture if fewer than `3` valid samples are collected before the window ends
- compute the output point as the component-wise median in local meter space, then convert back to lat/lon for storage
- store capture metadata including sample count, spread, and capture age
- add a cancel action that discards the in-progress sample window and returns to the pre-mark state

Do not add firmware changes yet unless UI-only sampling proves insufficient.

- [ ] **Step 4: Add short-line strict mode**

If line length is below `max(5 m, 4 * max(spread_p1, spread_p2))`:
- require the higher confidence tier
- keep the user on the review step if the captured points are too noisy
- show an explicit action-oriented warning instead of only a confidence label

- [ ] **Step 5: Run the UI tests and make them pass**

Run: `node tools/ui_preview/tests/check_firmware_web_ui.js`

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/wifi/web_ui.cpp tools/ui_preview/web_console.js tools/ui_preview/tests/check_firmware_web_ui.js
git commit -m "feat: sample gps points before track marking"
```

### Task 4: Upgrade The Offline Review Panel

**Files:**
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui.cpp`
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/web_console.js`
- Test: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/tests/check_preview.js`
- Test: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/tests/check_firmware_web_ui.js`

- [ ] **Step 1: Write failing preview tests for a true-scale review panel**

Expect the review panel to render:
- a north arrow
- meter-scale annotation
- correct aspect ratio preservation
- point uncertainty circles or halos
- line length and crossing heading metadata

- [ ] **Step 2: Run the preview tests and verify they fail**

Run: `node tools/ui_preview/tests/check_preview.js`

Expected: FAIL because the current SVG is only a stretched geometry sketch.

- [ ] **Step 3: Implement a local meter-space projection**

Project points into a local tangent plane relative to the line midpoint:
- convert lat/lon delta into meters using a local tangent-plane approximation with longitude scaled by `cos(lat_center)`
- preserve true aspect ratio
- fit the projected geometry into the SVG viewport with padding

- [ ] **Step 4: Surface offline review aids**

Add:
- north arrow
- line length in meters
- crossing direction arrow
- confidence badge
- uncertainty circle around each sampled point
- a short warning that v1 does not persist track-creation state across page refresh and the user should avoid closing or refreshing during creation

- [ ] **Step 5: Run both preview and UI tests**

Run:
- `node tools/ui_preview/tests/check_preview.js`
- `node tools/ui_preview/tests/check_firmware_web_ui.js`

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/wifi/web_ui.cpp tools/ui_preview/web_console.js tools/ui_preview/tests/check_preview.js tools/ui_preview/tests/check_firmware_web_ui.js
git commit -m "feat: improve offline track review geometry"
```

### Task 5: Add Optional Repeatability Check Before Final Create

**Files:**
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui.cpp`
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/web_console.js`
- Test: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tools/ui_preview/tests/check_firmware_web_ui.js`

- [ ] **Step 1: Write failing tests for repeatability mode**

Cover:
- after the first line is captured, the UI can request an optional confirmation measurement
- repeated measurement compares heading delta and midpoint offset
- high disagreement blocks creation for short lines
- close agreement upgrades confidence and allows creation

- [ ] **Step 2: Run the UI test and verify it fails**

Run: `node tools/ui_preview/tests/check_firmware_web_ui.js`

Expected: FAIL because no repeatability flow exists.

- [ ] **Step 3: Implement minimal repeatability comparison**

Add a simple comparison model:
- midpoint distance delta in meters
- heading delta in degrees
- pass/fail thresholds tuned more strictly for short lines than long ones

- [ ] **Step 4: Wire the review gate**

Ship v1 with repeatability **disabled by default** behind a clearly named constant such as `REPEATABILITY_CHECK_ENABLED = false`.

Implement the flow so the team can later switch between:
- mandatory for short lines only
- optional with warning
- disabled by default

The worker should ship the disabled-by-default behavior unless the human partner explicitly asks for a different default before implementation starts.

- [ ] **Step 5: Run the UI tests and make them pass**

Run: `node tools/ui_preview/tests/check_firmware_web_ui.js`

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/wifi/web_ui.cpp tools/ui_preview/web_console.js tools/ui_preview/tests/check_firmware_web_ui.js
git commit -m "feat: add track line repeatability checks"
```

### Task 6: Harden Track Save Diagnostics

**Files:**
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/track/track_store.cpp`
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/api_tracks.cpp`
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/storage/storage_recovery.cpp`
- Test: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tests_host/test_track_runtime.cpp`
- Create: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tests_host/test_track_creation_feedback.cpp`

- [ ] **Step 1: Extract or define a small pure helper for save-result classification**

Represent save outcomes like:
- directory missing
- directory create failed
- file open failed
- file write short
- file sync failed
- success

- [ ] **Step 2: Write the failing host-side tests**

Add tests that verify:
- the right save result maps to the right user-facing error message
- ambiguous `save failed` is no longer the only outcome

- [ ] **Step 3: Run the host test to verify it fails**

Run: `bash tools/run_host_tests.sh`

Expected: FAIL because the helper and message mapping do not exist yet.

- [ ] **Step 4: Implement save diagnostics with minimal behavior change**

In firmware:
- ensure `tracks/` exists before save
- reject start/finish pairs whose separation is less than `1 m` even if the client-side flow regresses
- log file open, write, and sync failures distinctly
- surface a clearer HTTP error string from `/api/tracks`

Avoid large refactors of the existing storage layer.

- [ ] **Step 5: Run host tests and relevant UI tests**

Run:
- `bash tools/run_host_tests.sh`
- `node tools/ui_preview/tests/check_firmware_web_ui.js`

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/track/track_store.cpp src/wifi/api_tracks.cpp src/storage/storage_recovery.cpp tests_host/test_track_creation_feedback.cpp tests_host/test_track_runtime.cpp
git commit -m "fix: improve track creation save diagnostics"
```

### Task 7: Add Documentation And Calibration Guidance

**Files:**
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/docs/PRD.md`
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/docs/ARCHITECTURE.md`
- Modify: `/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/tests_host/README.md`

- [ ] **Step 1: Document the new UX expectations**

Update docs to explain:
- visible coordinate readout poll cadence
- coordinate freshness badges and adaptive polling behavior
- fresh fetch before marking
- sampled point capture
- confidence tiers
- why short lines are higher-risk

- [ ] **Step 2: Add operator guidance**

Document practical usage tips:
- hold still during sampling
- prefer longer lines when possible
- remeasure if repeatability fails
- trust the confidence badge more than the old one-line status

- [ ] **Step 3: Run a docs sanity pass**

Check for any stale wording such as immediate one-shot point capture or map-like review wording.

- [ ] **Step 4: Commit**

```bash
git add docs/PRD.md docs/ARCHITECTURE.md tests_host/README.md
git commit -m "docs: describe gps confidence track creation flow"
```

## Verification Matrix

- UI behavior:
  - `node tools/ui_preview/tests/check_firmware_web_ui.js`
  - `node tools/ui_preview/tests/check_preview.js`
- Host-side pure logic:
- `bash tools/run_host_tests.sh`
- Manual device checks after firmware deploy:
  - confirm visible GPS text still updates while idle
  - confirm coordinate freshness badges age correctly, use coarse buckets, and do not flicker distractingly during fast polling
  - confirm pressing `Mark P1/P2` starts a timed sampling flow
  - confirm sector point marking uses the same sampled-capture flow
  - confirm sampling can be cancelled cleanly
  - confirm fewer than 3 valid samples does not store a point
  - confirm short noisy lines are blocked or strongly warned using the adaptive threshold
  - confirm heading remains correct after median-based sampled capture with a known reference line
  - confirm review panel preserves real aspect ratio
  - confirm save errors are specific and actionable

## Open Product Choices

- Whether confidence gating should hard-block low-confidence lines or allow override
- Whether to surface raw metrics such as spread radius and sample age to advanced users

## Post-landing Follow-up

- `src/wifi/web_ui.cpp` is already a large embedded HTML/JS container and Tasks 1–5 all add more logic there. After this feature set lands, plan a follow-up extraction of the track-creation JS into a dedicated embedded module or generated asset so future UI work does not keep expanding the same monolith.

---

## Review Notes

_Reviewed 2026-04-13 against current codebase state._

### 1. HDOP / Fix Quality Not Surfaced

The current plan uses satellite count + client-side spread + sample freshness as the v1 quality inputs. HDOP would be valuable, but the current parser only handles GGA and RMC sentences, so there is no existing HDOP data path today.

If the team later wants `sats >= 6 AND HDOP < 1.5` style gating, that requires new GSA parsing, new session-state fields, and `/api/status` exposure before the web UI can use it.

**Disposition:** Keep v1 satellite-count-based and treat HDOP as the first firmware-backed quality upgrade path, not as a nearly-free addition.

### 2. Adaptive Polling: ESP32 Web Server Load

Task 2 now limits itself to visible-coordinate freshness. The temporary higher-rate sampling cadence is defined in Task 3, which keeps the task dependency chain TDD-friendly.

The current rate is one `/api/status` request every 2000ms. If the plan pushes this to 400ms during sampling, that is 5× the HTTP request rate during a short window.

The ESP32 HTTP server runs on Core 1 as a single-connection handler. Each `/api/status` call acquires `session_mutex` to snapshot state. At 500ms intervals this is probably fine, but the plan should state:

- A minimum polling floor (e.g., no faster than 300ms) to avoid starving the display task which also runs on Core 1.
- That the adaptive rate only applies to the track-creation page, not the main dashboard.
- A fallback: if a poll response arrives slower than the interval, skip the next tick rather than queuing requests.

**Disposition:** Folded into Task 2 and Task 3. The plan now sets a 300ms floor, limits adaptive polling to the track-creation flow, and skips new polls while a request is in flight.

### 3. Sampled Capture: Statistical Method Needs Specification

Task 3 now specifies a fixed v1 policy: `2400 ms` sample window, `400 ms` cadence, minimum `3` valid samples, and component-wise median in local meter space.

For GPS coordinates this needs care:

- **Median latitude + median longitude independently** can produce a point outside the actual sample cluster in non-symmetric distributions.
- **Component-wise median** is fine for small clusters within a few meters, which is the expected use case here.
- **Trimmed mean** (discard highest and lowest, average the rest) is more robust when sample count is low (4–6 samples in a 2–3s window at 500ms polling).

The plan should specify: if `n < 5` samples collected (due to slow responses or transient GPS loss), does the capture fail or proceed with a lower confidence tier? This threshold matters because it decides whether a flaky Wi-Fi connection can cause a silent quality downgrade.

**Disposition:** Folded into Task 3. Fewer than 3 valid samples now fails the capture and prompts a retry.

### 4. Short-Line Strict Mode: Threshold Justification

Task 3 Step 4 uses 5m as the short-line threshold. The current minimum point separation is 2m. Consider:

- A 3m line with 2.5m allowed spread per point means the noise radius overlaps the line length. The start and finish points could literally swap.
- A 5m line with ~1m actual spread (after sampled capture) is reasonable.
- A 10m line is comfortable for most consumer GPS.

The plan should document why 5m was chosen. If the sampling improvement reduces per-point spread to ~1m, then a 3m line might become viable. The threshold should be tunable without code changes — possibly derived from the actual measured spread rather than a hardcoded constant.

**Recommendation:** Make the strict-mode threshold a function of the captured spread: `line_length >= K * max(spread_p1, spread_p2)` where K ≈ 3–5. This adapts automatically to actual signal quality rather than assuming a fixed noise level.

### 5. Review Panel Projection: cos(latitude) Correction

Task 4 Step 3 says "convert lat/lon delta into meters." The conversion must include the `cos(latitude)` correction for longitude:

```
dx_m = (lon2 - lon1) * 111320 * cos(lat_center)
dy_m = (lat2 - lat1) * 110540
```

The plan does not mention this explicitly. Without the correction, the review panel will still distort the aspect ratio at latitudes away from the equator — which is every real-world track. At Taiwan's latitude (~24°N), ignoring the correction stretches longitude by about 9%.

**Disposition:** Folded into Task 4 Step 3. The plan now explicitly calls out the local tangent-plane conversion with longitude scaled by `cos(lat_center)`.

### 6. State Persistence Across Page Refresh

The plan introduces a multi-step sampling flow: press Mark → wait 2–3s → capture stored → proceed to P2 or review. All of this state lives in client-side JavaScript variables.

If the user refreshes the page (common on mobile when the screen sleeps), the entire wizard state resets: P1 data, sample history, and stability buffer are all lost. The current code has this same issue, but it matters more now because:

- Sampling takes longer than instant capture, so the user invests more time per point.
- Confidence metadata is richer and harder to reconstruct.

The plan should decide:

- **Option A:** Accept the reset. The creation flow is short enough that re-capturing is not onerous. State this explicitly.
- **Option B:** Persist wizard progress in `sessionStorage` so a refresh resumes the flow. This is a small addition but adds complexity.

**Recommendation:** Option A for v1. But note it in the UX copy: "Do not close or refresh this page during track creation."

### 7. Server-Side Validation Gap

The plan focuses on client-side improvements. The `/api/tracks` POST handler currently performs no GPS quality validation — it trusts whatever coordinates the client sends. This is fine for a single-user embedded device, but the plan should acknowledge:

- Task 6 (save diagnostics) hardens the save path but not the input validation.
- If the confidence model ever becomes meaningful for data integrity, a minimal server-side sanity check (e.g., reject coordinates at `0,0` or with start/finish separation < 1m) would be cheap insurance.

The current code already rejects `sf_lat1 == 0 && sf_lon1 == 0`, but does not check separation. Adding a 1m server-side minimum separation check in Task 6 would be a one-line addition that closes the gap.

### 8. Task Dependencies and Ordering

The current task ordering (1→2→3→4→5→6→7) is mostly sequential, but some tasks have no real dependency:

- Task 4 (review panel geometry) is independent of Tasks 1–3. It could be implemented in parallel or even first since it fixes a standalone visual bug.
- Task 6 (save diagnostics) is firmware-side and independent of the client-side confidence work (Tasks 1–3).
- Task 5 (repeatability) depends on Task 3 (sampled capture) but not on Task 4.

If using parallel agents or worktrees, Tasks 4 and 6 could be done in parallel with the Task 1→2→3 chain. The plan should note this for faster execution.

### 9. Testing Coverage: Missing Edge Cases

The existing tests cover the happy path well. The plan adds tests for new states but should also cover:

- **GPS loss during sampling:** Fix drops out mid-sample window. The capture should fail cleanly, not return partial data.
- **Wi-Fi latency spike:** `/api/status` response takes > 1s during the 2–3s sample window. Fewer samples collected. How does confidence react?
- **Rapid sequential marks:** User presses Mark P1, immediately presses Mark P2 before P1 sampling finishes. The plan should define whether P2 mark is queued or rejected.
- **Negative spread values or NaN:** Defensive math for `haversineM()` with identical points (returns 0) or antipodal points.

### 10. Confidence Tier Labels May Confuse Non-Technical Users

Task 1 Step 4 uses labels like "Point captured with medium confidence." In a motorsport paddock context, the user is likely:

- Standing at the start/finish line holding a phone
- Wearing gloves, possibly in a hurry
- Not a GPS engineer

"Medium confidence" does not tell the user what to do. Consider action-oriented labels:

| Current Plan | Alternative |
|-------------|------------|
| `low confidence` | `Noisy — try again` |
| `medium confidence` | `Acceptable — short lines may drift` |
| `high confidence` | `Good — ready to save` |

The label should tell the user the **consequence**, not the abstract quality tier. The plan already has some action-oriented labels (`Need better GPS`, `Sampling point...`) — extend that approach to the post-capture labels too.

### 11. Missing: Cancel Sampling Action

Task 3 introduces a timed sampling window (2–3s) after pressing Mark. The plan does not mention a way to cancel mid-sample. If the user realizes they pressed the wrong point or moved, they need to abort without storing a bad point.

**Disposition:** Folded into Task 3. Sampling now includes an explicit cancel action and verification steps for clean abort behavior.

### First-Round Resolution Summary

| # | Status | Item |
|---|--------|------|
| 1 | Resolved | HDOP documented as not available in v1 (parser only handles GGA/RMC); deferred |
| 2 | Resolved | 300ms polling floor, skip-on-flight, track-creation-only scope added |
| 3 | Resolved | Minimum 3 valid samples required; fewer fails the capture |
| 4 | Resolved | Short-line threshold now `max(5m, 4 × max(spread))` — adaptive |
| 5 | Resolved | cos(lat) correction explicit in Task 4 Step 3 |
| 6 | Resolved | Page-refresh reset accepted; UX warning added in Task 4 Step 4 |
| 7 | Resolved | 1m server-side separation check added in Task 6 Step 4 |
| 8 | Open | Parallel execution opportunity for Tasks 4 and 6 noted but not in plan structure |
| 9 | Mostly resolved | GPS loss, cancel, rapid marks in Task 3 Step 1; Wi-Fi latency spike not explicit |
| 10 | Resolved | Action-oriented labels in Task 1 Step 4 |
| 11 | Resolved | Cancel sampling action in Task 3 |

---

## Review Notes — Round 2

_Second review 2026-04-13. Focused on gaps the first round missed and issues introduced by the updates._

### R2-1. Sector Marking Uses the Same Capture Path but the Plan Ignores It

The current code has two mark functions that both call `captureCurrentGpsPoint()`:

- `markStartFinishPoint(which)` — covered by the plan
- `markSectorPoint(id, which)` — not mentioned anywhere

Sectors use the exact same capture → store → heading flow as start/finish. When Task 3 replaces `captureCurrentGpsPoint()` with a sampled-capture flow, `markSectorPoint` will either:

- **Automatically inherit the change** if the sampling is wired at the `captureCurrentGpsPoint()` level. This is the cleanest path.
- **Break** if the new sampling flow is only wired into `markStartFinishPoint()` and sectors still call the old one-shot path.

The plan should state explicitly: the sampled capture applies to **all** point captures (start/finish and sectors), not just start/finish. If the implementation replaces `captureCurrentGpsPoint()` itself, this happens for free. But the tests (Task 3 Step 1) should include at least one sector-mark test case to verify it works end-to-end.

### R2-2. `web_ui.cpp` is Already 1333 Lines and Tasks 1–5 All Modify It

The file is already above the 800-line guidance in the coding standards. Tasks 1–5 each add significant JS logic to the same embedded `build_script_section()` string. After this plan, the file could easily exceed 1800+ lines.

The plan should acknowledge this and either:

- **Option A (v1):** Accept the growth. The embedded HTML/JS pattern is already established, and splitting mid-plan would add risk.
- **Option B (post-plan):** Add a follow-up task to extract the confidence/sampling JS into a separate embedded module or a compilable JS asset.

Recommendation: Option A for now, but add a note to Task 7 (documentation) or as a standalone follow-up item that the file needs extraction after this work lands.

### R2-3. Regression Safety Across Sequential Tasks

Tasks 1–5 all modify the same file and each builds on the previous one's code. The plan says "run the targeted UI tests and make them pass" per task, but does not explicitly require running **all previous tasks' tests** at each step.

For example, Task 3 could break a Task 1 state label if the sampling flow overrides the GPS readiness display. The test commands are the same (`check_firmware_web_ui.js`) so in practice they'd all run, but the plan should state explicitly: **every task step that runs tests must run the full UI test suite, not just the new tests**, to catch regressions.

### R2-4. Freshness Label Flicker

Task 2 Step 4 shows `Updated 0.3s ago` as an example freshness label. At the track-creation polling rate, this label would update every 300–500ms, causing the number to jump visually (`0.1s → 0.4s → 0.1s → 0.5s`). On a mobile screen in daylight this is distracting.

Recommendations:

- Update the freshness display at most once per second, not on every poll completion.
- Use coarser buckets: `Live` (< 1s), `1s ago`, `2s ago`, `Stale` (> 3s) rather than sub-second precision. The user does not need to know the exact age — they need to know whether it's trustworthy.

### R2-5. Task 5 Repeatability Default Not Committed

Task 5 Step 4 says the team can configure repeatability as mandatory/optional/disabled, but doesn't commit to a default. For an agentic worker executing this plan, this is an ambiguous product decision.

Either:

- Commit to a default in the plan (e.g., "disabled by default in v1, gated by a `REPEAT_CHECK_ENABLED = false` constant").
- Move the decision explicitly to "Open Product Choices" with a note that the implementer should ask before wiring the default.

Currently "Open Product Choices" still lists it, which is correct. But the task steps themselves should say what to ship, not leave it open.

### R2-6. Verification Matrix Gaps

The manual verification list (lines 402–410) should also cover:

- Sector point marking uses the sampled capture flow (not just start/finish)
- Short-line adaptive threshold blocks or warns correctly (e.g., mark two points 3m apart with 1.5m spread)
- Heading is correct after sampled-median capture (compare with known reference)
- Freshness label does not flicker on fast-polling mobile screen
- Cancel-sampling button is reachable and functional during the 2.4s window

### R2-7. Review Notes Section Cleanup

The Review Notes section now contains both first-round analysis (with interleaved "Disposition: Folded" markers) and this second-round review. For the agentic workers who will execute this plan, the first-round items are resolved context, not actionable items. Consider:

- Collapsing the first-round notes into the resolution summary table above.
- Keeping only the first-round items that remain **open** (item #8, partial #9) as live text.
- Keeping second-round items as the active review section.

This avoids an implementer re-reading 11 resolved items to find the 2 that still matter.

### R2 Summary

| # | Priority | Recommendation |
|---|----------|---------------|
| R2-1 | High | State that sampled capture applies to sector marks too; add sector test case |
| R2-2 | Low | Acknowledge web_ui.cpp size growth; plan post-landing extraction |
| R2-3 | Medium | Require full test suite run at each task, not just new tests |
| R2-4 | Low | Use coarser freshness buckets to avoid label flicker |
| R2-5 | Medium | Commit to a repeatability default (recommend: disabled in v1) |
| R2-6 | Medium | Add sector marks, short-line threshold, heading, cancel to verification matrix |
| R2-7 | Low | Collapse resolved first-round items into summary table; keep only open items |
