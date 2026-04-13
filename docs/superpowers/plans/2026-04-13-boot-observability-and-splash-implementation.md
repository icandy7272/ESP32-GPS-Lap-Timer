# Boot Observability And Splash Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a unified boot-status system that shows the approved branded splash immediately on power-up, keeps serial/TFT boot state in sync, and makes cold-boot stalls diagnosable without changing the runtime UI.

**Architecture:** Extract the boot state machine and TFT text/progress mapping into small top-level pure modules so they can be host-tested first. Then refactor the display boot path so TFT hardware is brought up before storage/GPS, draw the logo once, update only the mutable status zone, and finally rewire `setup()` to emit shared boot events, recovery warnings, GPS/track substeps, and crash breadcrumbs from one source of truth.

**Tech Stack:** Arduino + FreeRTOS firmware, TFT_eSPI, SdFat, PlatformIO, host-side C++17 `<assert.h>` unit tests via `bash tools/run_host_tests.sh`

---

## File Structure

**Create**

- `src/boot_status.h`
- `src/boot_status.cpp`
- `src/boot_presenter.h`
- `src/boot_presenter.cpp`
- `src/display/boot_logo_asset.h`
- `src/display/boot_logo_asset.cpp`
- `tests_host/test_boot_status.cpp`
- `tests_host/test_boot_presenter.cpp`

**Modify**

- `src/main.cpp`
- `src/display.h`
- `src/display/display.cpp`
- `src/display/display_boot.cpp`
- `src/display/display_internal.h`
- `tests_host/README.md`

**Why this split**

- `src/boot_status.*` owns stage/state transitions, serial line formatting, progress index decisions, and breadcrumb-friendly stage identity.
- `src/boot_presenter.*` owns the pure mapping from boot state to on-screen labels/details/progress so the UI copy stays testable and deterministic.
- `src/display/boot_logo_asset.*` owns the checked-in logo bytes and dimensions so DISPLAY does not depend on SD.
- `src/display/display_boot.cpp` owns only TFT drawing and partial redraw bookkeeping.
- `src/main.cpp` remains the boot orchestrator, but no longer hand-builds unrelated strings or duplicated boot rules.

### Task 1: Add A Pure Boot Status Core

**Files:**
- Create: `src/boot_status.h`
- Create: `src/boot_status.cpp`
- Test: `tests_host/test_boot_status.cpp`

- [ ] **Step 1: Write the failing host test for stage/state formatting and transitions**

```cpp
#include <string.h>
#include <assert.h>
#include "boot_status.h"

int main() {
    BootStatus status = boot_status_make();

    boot_status_begin(&status, BootStage::POWER, 12);
    assert(status.stage == BootStage::POWER);
    assert(status.state == BootState::START);
    assert(status.progress_segment == 0);

    boot_status_update(&status, BootState::OK, "rails stable", "rails stable");
    assert(status.state == BootState::OK);
    assert(status.display_detail[0] != '\0');

    char line[96];
    boot_status_format_line(status, 12, line, sizeof(line));
    assert(strstr(line, "[BOOT][0012 ms][POWER][ok]") != nullptr);
    return 0;
}
```

- [ ] **Step 2: Run the host test to verify it fails**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -I src -o /tmp/test_boot_status tests_host/test_boot_status.cpp src/boot_status.cpp
```

Expected: compile failure because `src/boot_status.cpp` and symbols do not exist yet.

- [ ] **Step 3: Write the minimal boot status module**

```cpp
enum class BootStage : uint8_t { POWER, DISPLAY, STORAGE, GPS, READY };
enum class BootState : uint8_t { START, OK, WARN, FAIL };

struct BootStatus {
    BootStage stage;
    BootState state;
    char display_detail[48];
    char serial_detail[80];
    uint32_t stage_started_ms;
    uint8_t progress_segment;
};

BootStatus boot_status_make();
void boot_status_begin(BootStatus* status, BootStage stage, uint32_t now_ms);
void boot_status_update(BootStatus* status,
                        BootState state,
                        const char* display_detail,
                        const char* serial_detail);
const char* boot_stage_name(BootStage stage);
const char* boot_state_name(BootState state);
void boot_status_format_line(const BootStatus& status,
                             uint32_t now_ms,
                             char* out,
                             size_t out_len);
```

- [ ] **Step 4: Re-run the host test to verify it passes**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -I src -o /tmp/test_boot_status tests_host/test_boot_status.cpp src/boot_status.cpp && /tmp/test_boot_status
```

Expected: exit `0`

- [ ] **Step 5: Run the full host test suite**

Run:

```bash
bash tools/run_host_tests.sh
```

Expected: existing host tests plus `test_boot_status` all pass.

- [ ] **Step 6: Commit**

```bash
git add src/boot_status.h src/boot_status.cpp tests_host/test_boot_status.cpp
git commit -m "feat: add boot status core"
```

### Task 2: Add A Pure Presenter For TFT Labels And Progress

**Files:**
- Create: `src/boot_presenter.h`
- Create: `src/boot_presenter.cpp`
- Test: `tests_host/test_boot_presenter.cpp`

- [ ] **Step 1: Write the failing host test for recovery, GPS wait, and track-detect copy**

```cpp
#include <string.h>
#include <assert.h>
#include "boot_presenter.h"

int main() {
    BootStatus status = boot_status_make();

    boot_status_begin(&status, BootStage::STORAGE, 100);
    boot_status_update(&status, BootState::WARN, "session recovered", "session recovered");
    BootView storage_view = boot_present(status);
    assert(strcmp(storage_view.stage_label, "STORAGE WARN") == 0);
    assert(strcmp(storage_view.detail_line, "session recovered") == 0);

    boot_status_begin(&status, BootStage::GPS, 200);
    boot_status_update(&status, BootState::OK, "track matched", "track matched: autodetect complete");
    BootView gps_view = boot_present(status);
    assert(strcmp(gps_view.stage_label, "GPS") == 0);
    assert(gps_view.active_segment == 3);
    return 0;
}
```

- [ ] **Step 2: Run the host test to verify it fails**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -I src -o /tmp/test_boot_presenter tests_host/test_boot_presenter.cpp src/boot_presenter.cpp src/boot_status.cpp
```

Expected: compile failure because `src/boot_presenter.cpp` does not exist yet.

- [ ] **Step 3: Implement the pure presenter**

```cpp
struct BootView {
    const char* stage_label;
    const char* detail_line;
    uint8_t completed_segments;
    uint8_t active_segment;
    bool fatal;
};

BootView boot_present(const BootStatus& status);
```

Mapping rules to encode:

- `POWER`, `DISPLAY`, `STORAGE`, `GPS`, `READY` always stay five segments.
- `STORAGE + WARN + session recovered` renders as `STORAGE WARN`.
- `GPS` remains `GPS` during fix wait, timeout warning, track detect, and short track confirmation.
- `READY` is segment `4`.

- [ ] **Step 4: Re-run the host test to verify it passes**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -I src -o /tmp/test_boot_presenter tests_host/test_boot_presenter.cpp src/boot_presenter.cpp src/boot_status.cpp && /tmp/test_boot_presenter
```

Expected: exit `0`

- [ ] **Step 5: Run the full host test suite**

Run:

```bash
bash tools/run_host_tests.sh
```

Expected: all host tests pass, including the new presenter test.

- [ ] **Step 6: Commit**

```bash
git add src/boot_presenter.h src/boot_presenter.cpp tests_host/test_boot_presenter.cpp
git commit -m "feat: add boot presenter mapping"
```

### Task 3: Add The Embedded Logo Asset And Unified Boot Renderer

**Files:**
- Create: `src/display/boot_logo_asset.h`
- Create: `src/display/boot_logo_asset.cpp`
- Modify: `src/display.h`
- Modify: `src/display/display_boot.cpp`
- Modify: `src/display/display.cpp`
- Modify: `src/display/display_internal.h`

- [ ] **Step 1: Check in the approved logo as a repo-local display asset**

Expected shape:

```cpp
// src/display/boot_logo_asset.h
extern const uint16_t kBootLogoWidth;
extern const uint16_t kBootLogoHeight;
extern const uint16_t kBootLogoPixels[];
```

Implementation notes:

- source image is the approved `/Users/wenchaodu/Downloads/logo.png`
- convert once into a tightly cropped RGB565 array
- target on-screen width is about `210-214 px` on the 320x240 splash
- store the generated bytes in `boot_logo_asset.cpp`, not inline in a header

- [ ] **Step 2: Add the new boot display API to `src/display.h`**

Add:

```cpp
struct BootStatus;

void display_boot_init();
void display_boot_update(const BootStatus& status);
```

Keep existing `display_show_*` declarations temporarily, but mark them as compatibility wrappers around the new renderer.

- [ ] **Step 3: Refactor `src/display/display.cpp` so runtime init does not reinitialize TFT hardware after boot**

Target shape:

```cpp
static bool s_tft_ready = false;
static bool s_backlight_ready = false;

static void ensure_tft_ready();
static void ensure_backlight_ready();

void display_boot_init() {
    ensure_tft_ready();
    draw_static_boot_frame();
    ensure_backlight_ready();
}

void display_init(...) {
    s_btn_queue = btn_display_q;
    s_spi_mtx = spi_mtx;
    s_session_mtx = session_mtx;
    ensure_tft_ready();
    ensure_backlight_ready();
    init_delta_sprite();
    ...
}
```

- [ ] **Step 4: Rebuild `src/display/display_boot.cpp` around one static frame plus partial redraws**

Required behaviors:

- draw black background + centered logo once
- reserve a bottom status zone
- update only stage label, detail line, and five progress segments
- render `start`, `ok`, `warn`, and `fail` states through the same layout
- preserve temporary wrappers:

```cpp
void display_show_gps_search(int sats) {
    BootStatus status = ...;  // compatibility shim only
    display_boot_update(status);
}
```

- [ ] **Step 5: Build firmware to verify the display refactor compiles**

Run:

```bash
~/.platformio/penv/bin/pio run
```

Expected: build succeeds with the new display API and embedded asset.

- [ ] **Step 6: Commit**

```bash
git add src/display.h src/display/display.cpp src/display/display_boot.cpp src/display/display_internal.h src/display/boot_logo_asset.h src/display/boot_logo_asset.cpp
git commit -m "feat: add boot splash renderer"
```

### Task 4: Rewire `setup()` To Emit Shared Boot Status And Show Splash Early

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/display.h`
- Modify: `src/display/display_boot.cpp`
- Modify: `src/boot_status.h`
- Modify: `src/boot_status.cpp`

- [ ] **Step 1: Write down the new boot order inside `src/main.cpp` before changing logic**

Target flow:

1. Serial + LED
2. early pin safe-state + power stabilization (`POWER`)
3. create `spi_mutex` and queues
4. `display_boot_init()` (`DISPLAY`)
5. storage/config/track load (`STORAGE`)
6. recovery warning if `storage_recovered`
7. GPS start + fix wait + track auto-detect (`GPS`)
8. `READY`
9. runtime task startup + runtime display loop handoff

- [ ] **Step 2: Replace ad-hoc `[BOOT]` logging with one helper path**

Use the new status core from `src/main.cpp`:

```cpp
static BootStatus s_boot_status;

static void boot_publish(BootStage stage,
                         BootState state,
                         const char* display_detail,
                         const char* serial_detail,
                         bool update_display = true) {
    if (s_boot_status.stage != stage) {
        boot_status_begin(&s_boot_status, stage, millis());
    }
    boot_status_update(&s_boot_status, state, display_detail, serial_detail);

    char line[96];
    boot_status_format_line(s_boot_status, millis(), line, sizeof(line));
    Serial.println(line);

    if (update_display) {
        display_boot_update(s_boot_status);
    }
}
```

- [ ] **Step 3: Move splash bring-up before storage init**

Concrete edits:

- keep the existing safe-state helper from `src/boot_sequence.cpp`
- create `spi_mutex` before `storage_init()`
- call `display_boot_init()` before any SD work
- remove the old `display_show_splash(); delay(1000);` path

- [ ] **Step 4: Fold recovery, GPS timeout, and track detect into the shared model**

Required mappings:

- storage init failure => `STORAGE warn`, continue boot
- `storage_recovered` => `STORAGE warn` + `session recovered`, held briefly
- GPS fix wait => repeated `GPS start/ok` updates with satellite detail
- GPS timeout at `30000 ms` => `GPS warn`, continue boot
- track auto-detect success => stay in `GPS`, update detail to `track matched`
- track miss => stay in `GPS`, short detail like `track not found`

- [ ] **Step 5: Replace the old boot full-screen helpers in `setup()`**

Remove direct usage of:

- `display_show_recovery()`
- `display_show_splash()`
- `display_show_track_found()`
- `display_show_ready()`

`setup()` should only talk in terms of boot stages/status updates.

- [ ] **Step 6: Build firmware**

Run:

```bash
~/.platformio/penv/bin/pio run
```

Expected: build succeeds and `src/main.cpp` uses only the shared boot-status path.

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp src/display.h src/display/display_boot.cpp src/boot_status.h src/boot_status.cpp
git commit -m "feat: wire unified boot status into setup"
```

### Task 5: Add Boot Breadcrumb Integration And Finish Crash Diagnostics

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/boot_status.h`
- Modify: `src/boot_status.cpp`

- [ ] **Step 1: Add an RTC-persistent coarse boot-stage breadcrumb**

Expected shape:

```cpp
RTC_NOINIT_ATTR uint8_t rtc_boot_stage;
RTC_NOINIT_ATTR uint8_t rtc_boot_state;
```

Update these whenever `boot_publish(...)` changes stage or state.

- [ ] **Step 2: Include the boot breadcrumb in crash persistence**

Extend the existing crash log write in `src/main.cpp`:

```cpp
int n = snprintf(buf, sizeof(buf),
    "reason=%s boot_stage=%s boot_state=%s core0=%d core1=%d "
    "wm:stor=%u disp=%u sess=%u wifi=%u lapt=%u\n",
    reason_str,
    boot_stage_name(static_cast<BootStage>(rtc_boot_stage)),
    boot_state_name(static_cast<BootState>(rtc_boot_state)),
    ...);
```

- [ ] **Step 3: Clear the boot breadcrumb only after a successful `READY` handoff**

Do not clear the breadcrumb at boot start. Clear it only after:

- `READY` has been published
- runtime task startup has succeeded enough to leave boot mode

- [ ] **Step 4: Re-run host tests and firmware build**

Run:

```bash
bash tools/run_host_tests.sh
~/.platformio/penv/bin/pio run
```

Expected: host tests stay green and firmware still builds.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp src/boot_status.h src/boot_status.cpp
git commit -m "feat: add boot breadcrumb diagnostics"
```

### Task 6: Final Verification And Documentation Touch-Up

**Files:**
- Modify: `tests_host/README.md`

- [ ] **Step 1: Document the new host-testable modules**

Update `tests_host/README.md` to include:

- `test_boot_status.cpp`
- `test_boot_presenter.cpp`

- [ ] **Step 2: Run the full host test suite**

Run:

```bash
bash tools/run_host_tests.sh
```

Expected: all host tests pass with `0 failed`.

- [ ] **Step 3: Run the firmware build**

Run:

```bash
~/.platformio/penv/bin/pio run
```

Expected: build succeeds.

- [ ] **Step 4: Perform manual cold-boot validation on device**

Checklist:

- power-cycle the board fully at least 5 times
- confirm splash appears immediately after power-on
- confirm background is pure black and logo is centered
- confirm status text remains small and uncluttered
- confirm SD-missing boot shows `STORAGE warn` and still reaches runtime
- confirm recovered-session boot shows `session recovered` before `GPS`
- confirm slow/no-fix GPS boot remains in `GPS` and then continues after timeout
- confirm GPS fix + nearby track keeps the coarse stage at `GPS` while showing track detail
- confirm a successful boot reaches `READY` and then hands off to the runtime display

- [ ] **Step 5: Commit**

```bash
git add tests_host/README.md
git commit -m "docs: update boot test coverage notes"
```

## Execution Notes

- Keep all new host-testable logic at the top level of `src/` so `bash tools/run_host_tests.sh` picks it up automatically.
- Do not let `display_boot_init()` depend on `storage_init()` or on the runtime display task.
- Avoid loading the logo from SD; DISPLAY must stay independent from STORAGE.
- Favor compatibility wrappers during the refactor rather than deleting all old boot helper names in one shot.
- Resist adding speculative timeouts for `DISPLAY` or `STORAGE`; only `GPS` has an explicit timeout requirement in the approved spec.
- If the embedded logo data noticeably inflates compile or link time, keep the generated array in its own `.cpp` translation unit and expose only declarations through the header.

## Verification Commands

```bash
bash tools/run_host_tests.sh
~/.platformio/penv/bin/pio run
```
