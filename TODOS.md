# Hardware Validation TODOs

## Before v1.0 Release

- [ ] Verify GPS cold-start time under open sky (target < 30s)
- [ ] Measure lap timing accuracy against known reference (target +/- 0.02s)
- [ ] SD card endurance test: 2-hour continuous recording session
- [ ] Power-loss recovery test: pull power mid-session, verify .tmp rename
- [ ] TFT readability in direct sunlight at max brightness
- [ ] WiFi range test: phone at 10m / 20m from device
- [ ] Thermal test: 1-hour session in enclosure at 40C ambient
- [ ] Battery runtime measurement (if battery board is connected)
- [ ] Verify lap list empty-state transition on real hardware: start with `No laps yet`, run a first real lap, confirm the text clears cleanly and does not ghost when paging away and back

### GPS Precision Validation (post-2026-04-23 Kalman + routing work)

Commits in scope: `598e9fb` (alpha-beta Kalman + route filtered fix to live map),
`088e71c` (escape-hatch reset + filtered/raw API split), `9d25328` (test runner
.deps fail-on-missing).  None of these have run on hardware yet — the previous
flash attempt couldn't see the board on USB.

- [ ] **Walk test the new filter on live_map.**  Board on the ground, stationary:
      green dot wobble should drop from ±3 m to ≤25 cm.  Walking pace: smooth path
      tracking with no perceptible lag.  Stopping: position should snap and lock
      instantly (stationary_hold + Kalman together).  This is the validation that
      decides whether the Kalman tuning is right or needs another pass.
- [ ] **Kart-track test at 30–80 km/h.**  Real use case.  Verify (1) lap times
      vs stopwatch reference, (2) delta digit stability through corners, (3) VBO
      file path cleanness on the Race Studio replay, (4) phone live map view
      (now also filtered) is usable for the driver / coach during a session.
- [ ] **Repeatable crossing scatter test.**  On a real start/finish line, record
      at least 20 same-direction passes and compare the crossing timestamp
      scatter before claiming any professional-grade timing improvement.
- [ ] **Delta jump review.**  Record repeated laps and inspect Delta continuity
      around corners, parallel track segments, and start/finish wrap.
- [ ] **Decide if Open Gap finding 4 needs to ship in v1.0** — GSA is currently
      disabled at runtime, so the quality scoring runs on partial DOP metadata
      (HDOP-first).  Either turn GSA on at low cadence and accept the UART cost,
      or accept the limitation and document it in `docs/PRD.md` / quality-tier
      docs.  See `docs/CODEX_REVIEW_2026-04-23_gps-precision-followup.md` §4.
- [x] **Bundle GPS queue payload + route Delta through aligned `match_fix`.**
      Implemented 2026-04-25: `gps/gps_fix.cpp` now pushes a `GpsFixBundle`
      containing `raw_fix`, aligned `match_fix`, and `match_valid`; lap timer
      keeps raw fixes for crossing/VBO/reference capture and routes only Delta
      through the matched path.
- [ ] **Prototype UBX NAV-PVT receiver-time path.**  Separate spike branch:
      configure NAV-PVT, parse `iTOW` plus `fixType` / `hAcc` / `sAcc` /
      `headAcc` / `pDOP`, map receiver epoch to `esp_timer` with an offset
      smoother, and evaluate whether GPS UART should move from 115200 to
      230400 or 460800 baud.
- [ ] **Promote quality gates beyond HDOP-first scoring.**  Once GSA or
      NAV-PVT accuracy fields are available, feed them into crossing gates
      and Delta match scoring instead of relying mainly on satellites + HDOP.

## Deferred to v1.1

- [ ] Multi-track candidate selection UI (PR8 scope reduction)
- [ ] Battery gauge IC integration (no hardware IC in v1.0)
- [ ] OTA firmware update via WiFi
- [ ] Bluetooth LE for lower-power phone connectivity
- [ ] Redesign LIVE sector preview/workflow to show current-sector partial delta with independent coloring in the workbench and firmware data flow
- [ ] Add a dashboard/diagnostics workbench prototype for live status and historical metrics such as satellites, actual vs configured rate, drops, and NMEA tail
- [ ] Follow the display refresh and storage roadmap in `docs/superpowers/plans/2026-04-18-display-refresh-and-storage-roadmap.md`, prioritizing software-only perceived-refresh work first, PSRAM-backed SD batching second, and soldering-time hardware reservations throughout
- [ ] Follow the GPS algorithm optimization roadmap in `docs/superpowers/plans/2026-04-18-gps-algorithm-optimization-roadmap.md`, prioritizing fix-quality scoring and path separation before stronger filtering or fusion work

## Hardware Migration TODOs

These are post-PCB-soldering revert tasks; full procedure with file paths and
verification steps lives in
`~/.claude/projects/-Users-wenchaodu-Documents-Claude-code-projects-ESP32-track-GPS/memory/project_pcb_migration.md`.

- [x] **Stage A.1 (2026-04-29, USB-verified):** CPU `f_cpu` 160 → 240 MHz in
      `platformio.ini` and SD `SD_SPI_MHZ` 4 → 25 in
      `src/storage/storage_internal.h`. Boot log on USB power confirmed
      `CPU  : 240 MHz`, `boot splash ready`, no BROWNOUT / Guru / PANIC /
      watchdog. SD/GPS were on battery rail so SD-25-MHz path still
      unverified.
- [ ] **Stage A.2 (battery boot validation):** with battery connected so SD
      and GPS rails are powered, capture boot log and confirm:
      - SD initializes cleanly at 25 MHz (no `[storage] SD card init failed`).
      - GPS comes up on configured baud and emits NMEA.
      - Run a short walk session and verify SD `boot_log.txt` has no
        BROWNOUT entries and no SDFat write errors.
      If SD still fails, drop `SD_SPI_MHZ` incrementally (20 → 16 → 12 → 8)
      until clean, and update `src/storage/storage_internal.h` comment with
      the validated ceiling.
- [ ] **Stage A.3 (TFT clock sweep — 2026-04-29 deferred):** TFT
      `-DSPI_FREQUENCY=27000000` / `-DSPI_READ_FREQUENCY=20000000` produced
      a white screen on the hand-soldered PCB. Currently held at the
      breadboard `4000000` / `6000000` values, which are confirmed working.
      Sweep upward in steps (10 / 15 / 20 / 27 MHz), reflash + visually
      check that the boot splash, recording UI, and sector view all draw
      cleanly at each step. Stop at the highest pair that still draws
      cleanly and update `platformio.ini` plus the comment near the
      `BREADBOARD_OVERRIDE` line. The matching `SPI_READ_FREQUENCY` should
      stay roughly 1.4× lower than `SPI_FREQUENCY`.
- [ ] **Stage B (before going to a real track):** stop using the walking-test
      env. Default `pio run` already builds production thresholds — walking
      mode is now isolated in `[env:esp32-s3-devkitc-1-walking-test]`
      (commit `faf2963`), so just don't pass
      `-e esp32-s3-devkitc-1-walking-test` on the upload command.  Verify
      production thresholds engage (`HEADING_WINDOW` 180° → 60°,
      `ARM_DISTANCE_M` 3 → 10 m, lap-time floors 5 s → 15 s for both
      lap_timer and session).

### Plan B — split SD off the shared SPI bus (contingency)

**Trigger:** only pursue if, after Stage A (SD SPI at 25 MHz), walk test or
track test shows visibly perceptible LCD frame drops coinciding with SdFat
block flushes during recording sessions.  Do NOT pre-wire — shared SPI is the
default and the `spi_mutex` serialization is fully debugged.

**Software mitigations to try first before touching wiring:**
1. Profile whether VBO flush stalls actually coincide with dropped LCD frames
   (check `[lcd]` mirror cadence vs storage_task activity in boot_log.txt).
2. Batch VBO writes — accumulate N entries in RAM, flush once per second
   instead of per-fix.
3. Add `taskYIELD()` between SdFat batch-flush chunks.
4. Pin `storage_task` to core 1, keep `display_task` on core 0 (verify current
   affinity in `src/main.cpp` task_create calls).

**Hardware Plan B (perfboard rewire, ~30 min with flying wires):**

ESP32-S3 SPI3 goes through the GPIO matrix, so pin choice is flexible.
Move SD's MOSI and SCLK to a dedicated SPI3 bus; MISO (GPIO 13) stays put —
it was already SD-only.

| Signal | From (shared SPI2) | To (dedicated SPI3) |
|---|---|---|
| `PIN_SD_MOSI` | GPIO 11 | GPIO 38 |
| `PIN_SD_SCLK` | GPIO 12 | GPIO 39 |
| `PIN_SD_MISO` | GPIO 13 | GPIO 13 (unchanged) |
| `PIN_SD_CS`   | GPIO 42 | GPIO 42 (unchanged) |

Code changes:
- `src/pins.h`: update `PIN_SD_MOSI` and `PIN_SD_SCLK` constants.
- `src/storage/` (wherever `SD.begin(...)` is called): pass the SPI3 instance
  instead of the default (`SPI.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI)`
  followed by `SD.begin(PIN_SD_CS, SPI, ...)` or use `SPIClass SPIhw(HSPI)`).
- `src/main.cpp` / display path: remove the SD-related `spi_mutex` takes from
  the storage side only; TFT stays on the same mutex (or the mutex can be
  retired entirely if nothing else contends).

Avoid: strapping pins (0, 3, 45, 46), USB D+/D- (19, 20), JTAG (21),
already-used pins (4, 5, 8, 9, 10, 11, 12, 13, 17, 18, 42, 47).  GPIO 38/39
are contiguous on the DevKitC-1 header and have no side duties.
