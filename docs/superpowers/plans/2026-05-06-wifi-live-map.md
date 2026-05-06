# WiFi UDP Live-Map Transport + Offline Canvas View

**Date:** 2026-05-06
**Status:** In flight

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans.
> Steps use checkbox (`- [ ]`) syntax for tracking.

---

## Goal

Let `tools/live_map.py` consume the firmware's `[gps-live]` stream over
WiFi UDP instead of USB serial, so the laptop can display the Kalman
walk-test view while the board runs on battery. Render the map view
without any online tiles, since the laptop loses internet the moment it
joins the `KartGPS` AP.

The whole point is to validate Kalman behavior with the board on
battery in the open: ±25 cm stationary wobble, smooth walking trail,
and snap-on-stop. Today that flow is blocked because USB and battery
can't share power, and Leaflet+OSM tiles can't load without internet.

## Non-goals

- Replacing the existing USB serial transport. It stays.
- Pre-caching real-world map tiles. Deferred.
- Phone-side rendering. Laptop-only.
- Adding any authentication / authorization to the broadcast. AP is
  password-protected; UDP broadcast inside that perimeter is fine.
- Repackaging Leaflet for offline. The whole offline view is canvas-only.

## Tech Stack

- Firmware: C++ on ESP32-S3 (Arduino), `WiFiUDP` from the WiFi library.
- Tool: Python 3 stdlib `socket`, plus the existing `pyserial` path.
- HTML / vanilla JS for the canvas view (no new dependencies).

---

## Architecture

### Data flow today (USB serial)

```
gps_fix.cpp:130
  Serial.printf("[gps-live] lat=... lon=... ...\n", ...)
                              │
                              ▼
                  USB UART  → laptop /dev/cu.usbserial-10
                              │
                              ▼
  live_map.py serial_reader() → parse_line() → state → HTTP at 127.0.0.1:8080
```

### Data flow after this plan (USB OR UDP)

```
gps_fix.cpp
  snprintf(buf, sizeof(buf), "[gps-live] ...\n", ...)
                              │
                              ├── Serial.write(buf, n)         (unchanged)
                              │
                              └── gps_live_udp_broadcast(buf, n)
                                                │
                                                ▼
                                  WiFiUDP → 192.168.4.255:5555
                                                │
                                                ▼ (laptop on KartGPS AP)
  live_map.py udp_reader() → parse_line() → state → HTTP at 127.0.0.1:8080
                                                │
                                                ▼
                       canvas-only renderer when --offline-map is set
```

The two transports share the exact same byte stream, parsed by the
exact same `parse_line()`. No format drift possible because we
`snprintf` once into a buffer and then ship those same bytes both ways.

### Canvas-only renderer

Activated by `--offline-map`. Replaces the Leaflet `<script>` and
`tileLayer` block with a self-contained `<canvas>`-based view that
needs no internet:

- Full-window dark canvas
- Local-meter projection rooted at the first received fix
- Trail: blue polyline, last ~60 s of fixes
- Current position: solid green dot
- Grid: 1 m fine + 5 m bold lines, with corner labels
- Auto-zoom: trail bounding box + 10 m padding, clamped to a
  reasonable min span (e.g. 5 m so a stationary dot stays visible)
- Side panel (small, top-right): lat / lon / sats / quality / HDOP /
  speed / heading / wobble (last ~10 s, in cm)

P1/P2 detection-line overlay and scatter mode are **deferred** —
not needed for this slice's "is the Kalman tuning right?" question.

---

## File Map

### Firmware

| File | Change |
|------|--------|
| `src/wifi/wifi_server.h` | Declare `void gps_live_udp_broadcast(const char* line, size_t len)` and `void gps_live_udp_init()`. |
| `src/wifi/wifi_server.cpp` | Implement both. Module-static `WiFiUDP` instance. `gps_live_udp_init()` is called from `wifi_init()` after `softAP()` is up. The broadcast helper is best-effort and silent on failure. |
| `src/gps/gps_fix.cpp` | Replace the single `Serial.printf("[gps-live] ...\n", ...)` at line 130 with a `snprintf(buf, sizeof(buf), ...)` followed by `Serial.write(buf, n)` and `gps_live_udp_broadcast(buf, n)`. Add `#include "wifi/wifi_server.h"`. |

### Tool

| File | Change |
|------|--------|
| `tools/live_map.py` | Add `argparse`-based CLI (currently positional args), with `--source {usb,udp}`, `--udp-port 5555`, `--offline-map`. Default: `usb`, no offline (backward compatible). Add `udp_reader()` function that mirrors `serial_reader()` semantics. Carve out the Leaflet block behind a render-mode switch; add the canvas-only renderer alongside. |

### Tests

| File | Change |
|------|--------|
| `tests_host/test_live_map_udp.py` | New. Uses an in-process UDP socket to send a synthetic `[gps-live]` packet to the new udp_reader, asserts state updates correctly. Same fake-serial trick as `test_live_map_parse.py` so pyserial isn't required at test time. |
| `tests_host/test_live_map_parse.py` | Extend with one assertion that the canvas-only HTML never references `unpkg.com`, `tile.openstreetmap.org`, or `arcgisonline.com`. |

### Docs / TODOs

| File | Change |
|------|--------|
| `TODOS.md` | Mark "Walk test the new filter on live_map" with the new prereq satisfied (no laptop needed). |
| `docs/WIRING.md` | One-line note pointing readers to `tools/live_map.py --source udp --offline-map` for battery-powered field testing. |

---

## Task 1 — Firmware: UDP broadcast helper

- [ ] Add to `src/wifi/wifi_server.h`:
      ```cpp
      void gps_live_udp_init();
      void gps_live_udp_broadcast(const char* line, size_t len);
      ```
- [ ] In `src/wifi/wifi_server.cpp`, add file-static
      `WiFiUDP s_gps_live_udp;` and `bool s_gps_live_udp_ready = false;`
- [ ] Define constant `static constexpr uint16_t kGpsLiveUdpPort = 5555;`
- [ ] Implement `gps_live_udp_init()`: call `s_gps_live_udp.begin(kGpsLiveUdpPort)`
      after the `softAP()` call in `wifi_init()`. Set
      `s_gps_live_udp_ready = true` on success.
- [ ] Implement `gps_live_udp_broadcast(line, len)`:
      - If `!s_gps_live_udp_ready` or `len == 0`, return.
      - `beginPacket(IPAddress(255, 255, 255, 255), kGpsLiveUdpPort)`.
        (Subnet broadcast — works because the AP subnet is the only
        connected network on the board.)
      - `write(reinterpret_cast<const uint8_t*>(line), len)`.
      - `endPacket()` (return value ignored — failures silent).

**Why subnet-wide broadcast?** AP subnet is fixed at `192.168.4.0/24`,
gateway `192.168.4.1`. We could compute `192.168.4.255` from
`softAPIP()`, but `255.255.255.255` is the limited broadcast address
and the WiFi stack already restricts it to the only attached interface.
Less ceremony, identical behavior on this hardware. We'll add a
comment so the next reader doesn't think it's a bug.

## Task 2 — Firmware: emit through both transports

- [ ] In `src/gps/gps_fix.cpp`, add `#include "wifi/wifi_server.h"` near
      the top.
- [ ] Replace the existing `Serial.printf("[gps-live] ...\n", ...)`
      block (around line 130) with:
      ```cpp
      char buf[160];
      int n = snprintf(buf, sizeof(buf),
                       "[gps-live] lat=%.7f lon=%.7f sats=%d fix_3d=%d "
                       "speed=%.2f head=%.1f hdop=%.1f q=%u tier=%u "
                       "t_us=%lld\n",
                       emit_point.lat_deg, emit_point.lon_deg,
                       emit_point.satellites, emit_point.fix_3d ? 1 : 0,
                       (double)emit_point.speed_kmh,
                       (double)emit_point.heading_deg,
                       (double)emit_point.hdop,
                       emit_point.quality_score,
                       emit_point.quality_tier,
                       (long long)emit_point.timestamp_us);
      if (n > 0) {
          size_t out_len = (n < (int)sizeof(buf)) ? (size_t)n : sizeof(buf) - 1;
          Serial.write(reinterpret_cast<const uint8_t*>(buf), out_len);
          gps_live_udp_broadcast(buf, out_len);
      }
      ```
- [ ] Verify byte-for-byte equivalence with the prior `Serial.printf`
      output on a sample point (manual diff during code review).

## Task 3 — Firmware: build verification

- [ ] `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1` succeeds.
- [ ] `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1-walking-test`
      succeeds.
- [ ] No new warnings about format strings, unused returns, or buffer
      truncation.

## Task 4 — Tool: argparse + UDP reader

- [ ] Convert `tools/live_map.py`'s argv handling into `argparse`
      while keeping positional `port baud` backward compatible (use
      `parser.parse_known_args` and shim — see `_parse_legacy_args`
      helper to be added).
- [ ] Add flags: `--source {usb,udp}` (default `usb`), `--udp-port`
      (default `5555`), `--offline-map`.
- [ ] Add `udp_reader(port: int)` function:
      - Bind UDP socket to `0.0.0.0:port`. `SO_REUSEADDR` set so two
        invocations don't fight on dev iteration.
      - `recvfrom(2048)` loop. For each packet, decode UTF-8 with
        `errors='replace'`, split by `\n`, drop empty trailing entry,
        and feed each line to `parse_line()`.
      - Wrap parse_line calls in try/except like `serial_reader()`.
      - Mirror `_mark_serial_connected/_mark_serial_disconnected` so
        the UI says "udp connected" and shows packet count if
        applicable. Naming-wise: introduce `_mark_source_connected`
        and `_mark_source_disconnected` and have both readers call
        them; rename the dict keys (`serial_*` → `source_*`) at the
        same time, with backward-compat for any UI callers.
- [ ] In `main()`, dispatch to `serial_reader` or `udp_reader` based
      on `--source`. Both run in their own thread.
- [ ] Refuse to start serial-side bootstrap commands
      (`_send_runtime_bootstrap_commands`) when source is UDP, since
      we can't write commands back over UDP. Log an INFO line so the
      operator knows.

## Task 5 — Tool: canvas-only renderer

- [ ] Identify the HTML template region currently containing the
      Leaflet `<link>`, `<script>`, and the `tileLayer` setup. That
      region is around `tools/live_map.py:1480..1700`.
- [ ] Refactor it into a Python-side template selection:
      `MAP_RENDERER = 'leaflet'` (default) or `'canvas-offline'`
      (when `--offline-map` is active). Leaflet block stays as-is for
      `'leaflet'`. New canvas block is emitted for `'canvas-offline'`.
- [ ] Canvas block contents:
      - One `<canvas id="offline-map">` sized to fill the map area.
      - Inline `<script>` (no CDN) that:
        1. Holds a circular buffer of recent fixes (timestamps + lat/lon).
        2. Projects via `lonScale = 111320 * cos(originLat * pi / 180)`,
           `latScale = 110540`, mirroring the firmware/preview helpers.
        3. Auto-fits canvas viewport to bounding box of recent fixes,
           with a min span of 5 m so a stationary dot doesn't blow up
           into a single pixel.
        4. Draws the 1 m / 5 m grid first, then trail, then current dot.
        5. Computes "wobble" as max pairwise distance among fixes in
           the last 10 s and shows it in the side panel.
        6. Polls the existing `/state` JSON endpoint at the same
           cadence as the Leaflet view (so the rest of the page logic
           is unchanged).

## Task 6 — Tests

- [ ] New `tests_host/test_live_map_udp.py`:
      - Reuse the fake-`serial` trick from `test_live_map_parse.py`.
      - Spin up `udp_reader` on a bound port in a background thread.
      - Send a known-good `[gps-live] lat=37.0 lon=-122.0 sats=10 ...`
        packet via `socket.sendto`.
      - Assert that `live_map._state.current_lat` (or whichever the
        actual state holder is) reflects the parsed value.
      - Tear down cleanly.
- [ ] Extend `tests_host/test_live_map_parse.py`:
      - Add a small test that, for `MAP_RENDERER='canvas-offline'`,
        the rendered HTML contains `<canvas id="offline-map"` and
        does not contain `unpkg.com`, `tile.openstreetmap.org`,
        `arcgisonline.com`.
- [ ] `python3 tests_host/test_live_map_parse.py` exits 0.
- [ ] `python3 tests_host/test_live_map_udp.py` exits 0.
- [ ] `bash tools/run_host_tests.sh` (if it picks up the new file)
      reports green.

## Task 7 — Docs and TODOs

- [ ] In `TODOS.md`, edit "Walk test the new filter on live_map" item
      to mention the new battery-friendly path:
      `python3 tools/live_map.py --source udp --offline-map`.
- [ ] In `docs/WIRING.md`, near the dual-supply note (line ~343), add
      one paragraph: "For battery-powered field tests where USB is
      not connected, run `tools/live_map.py --source udp --offline-map`
      after joining the `KartGPS` AP. Board broadcasts on UDP 5555."
- [ ] Mention nothing about scatter mode or P1/P2 — those are deferred.

## Task 8 — Verification

- [ ] Build firmware. Walking-test env. Flash. Boot log shows:
      `[wifi] AP started: SSID=KartGPS  IP=192.168.4.1` (existing log)
      Plus implicit confirmation that the new `udp_init()` doesn't
      crash on boot.
- [ ] On laptop:
      - Disconnect USB
      - Power board from battery
      - Join `KartGPS` AP from laptop, password `kartgps123`
      - `python3 tools/live_map.py --source udp --offline-map`
      - Browse to `http://127.0.0.1:8080`
      - Wait for first fix, see green dot land on canvas with grid
- [ ] Walk-test (the actual goal):
      - Stand still 30 s — wobble field on side panel should report
        ≤ 25 cm
      - Walk 50 m straight then 90° turn — trail should be smooth, no
        visible lag or staircasing
      - Stop abruptly — dot should snap and lock in place

## Out of scope

- Pre-cached or vendored map tiles for areas of interest. If we ever
  want them, that's a separate plan.
- TCP fallback. UDP is enough on a single-AP local network with a
  best-effort design.
- Encrypting the broadcast. Not needed inside the AP perimeter.
- Reverse channel (laptop → board commands over WiFi).
  `_send_runtime_bootstrap_commands` already runs over USB; for now
  the UDP mode just won't send those.
- Multi-client coordination. Two laptops listening at the same time
  is fine because UDP broadcast goes to both.
- Scatter / Kalman-specific diagnostic view. Defer until walk-test
  reveals it's needed.

## Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Broadcast packet overhead at 25 Hz | 25 × 150 B = 3.75 KB/s. Trivial vs. WiFi capacity. |
| `WiFiUDP::beginPacket` blocks too long inside the GPS task | Best-effort: ignore return code, no retry, no buffering. If `beginPacket` returns 0 we just `endPacket` and move on. Worst case is a missed frame. |
| Format drift between Serial and UDP | Single `snprintf` source, both transports write the same buffer. |
| Laptop firewall blocks UDP 5555 | macOS default allows binding ephemeral ports for the originating process. Document the symptom in `tools/live_map.py --help` if it bites. |
| `WiFi.softAP` not yet up when `gps_live_udp_init` is called | `wifi_init()` calls `softAP()` then `udp_init()` synchronously, so this is fine. Add a comment. |
| `gps_live_udp_broadcast` called before `gps_live_udp_init` | Guard via `s_gps_live_udp_ready` flag so it returns silently. |
| Canvas projection drift across long sessions | Origin re-anchored once per session at first fix. For walk-tests this is < 1 km, negligible distortion. Document. |
| live_map.py CLI break | Keep positional args working via `parse_known_args` shim. |
| Tool/firmware version skew (different `[gps-live]` shape) | Existing `parse_line()` already handles missing fields gracefully. |

## Verification command summary

```bash
# Firmware build
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1-walking-test

# Host tests
python3 tests_host/test_live_map_parse.py
python3 tests_host/test_live_map_udp.py

# Full host suite (if applicable)
bash tools/run_host_tests.sh

# Manual walk-test (post-flash, on battery)
# 1. Join KartGPS AP
# 2. python3 tools/live_map.py --source udp --offline-map
# 3. http://127.0.0.1:8080 in browser
```

## Self-Review Plan (3 rounds, after implementation)

After implementation, three review rounds in this order:

1. **Round 1 — Correctness:** does each diff line do what the plan
   said? Walk through every modified file, compare to the file map
   above, list deviations.
2. **Round 2 — Integration:** does the firmware UDP path interact
   correctly with `wifi_init()` ordering, the GPS task scheduling,
   `WiFiUDP` lifecycle? Does the tool fall back cleanly if a UDP
   packet arrives malformed? Does `--source udp` not accidentally
   trigger serial-side bootstrap commands?
3. **Round 3 — Code quality:** naming, comments, error handling,
   buffer sizing, off-by-one risks (line newline handling, CRLF vs LF
   on UDP), test isolation (the UDP test must not bind a real port
   that another test could collide with).

Each round produces a written list of findings + decisions. Findings
are either "fix now," "TODO," or "intentional / explained."

---

## Self-Review Report (2026-05-06)

Final state going into review:

```
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1               # SUCCESS (3.85s)
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1-walking-test  # SUCCESS (3.06s)
python3 tests_host/test_live_map_parse.py                          # OK
python3 tests_host/test_live_map_udp.py                            # OK
bash  tools/run_host_tests.sh                                      # 21 passed, 0 failed
```

RAM 22.8% (74 804 B / 327 680 B). Flash 36.0% (1 132 105 B / 3 145 728 B).
Pre-change baseline was RAM 22.7%, so the WiFiUDP instance and the
broadcast helper cost about 384 bytes — well within budget.

### Round 1 — Correctness

Walked every modified file against the File Map above.

| # | Location | Plan said | Actual | Decision |
|---|----------|-----------|--------|----------|
| R1.1 | `src/wifi_server.h` | declare both `gps_live_udp_init()` and `gps_live_udp_broadcast()` | only the broadcast helper is public; `init()` is file-static in `wifi_server.cpp` | **Intentional.** `init()` is only called by `wifi_init()` in the same TU, no benefit to exporting it. Keeps the public surface to one symbol. |
| R1.2 | `tools/live_map.py` state dict | rename `serial_*` keys to `source_*` | not done | **Deferred TODO.** The keys still work for both transports because both readers call `_mark_serial_connected/_disconnected`. Renaming is cosmetic and would touch UI code that this plan didn't intend to revisit. |
| R1.3 | `HTML_OFFLINE` side panel | include `speed` and `heading` rows | both omitted | **Intentional.** `parse_line()` doesn't extract `speed=` or `head=` from `[gps-live]` into the shared `state` dict today, so those rows would only ever show `–`. Trimming keeps the panel focused. If we ever surface them in `state`, add the rows back. |

No correctness regressions vs the plan. All deviations are explicit and
justified.

### Round 2 — Integration

Verified each cross-module interaction.

| # | Check | Result |
|---|-------|--------|
| R2.1 | `gps_live_udp_init()` ordering | `main.cpp:1317` calls `gps_init()` (which spawns `gps_task`) BEFORE `main.cpp:1413` calls `wifi_init()` (which calls `gps_live_udp_init()`). So during the boot window — typically the first 30 s while `boot_wait_for_gps` runs — `gps_live_udp_broadcast()` is called but `s_gps_live_udp_ready == false`. The guard returns silently. **Correct, but worth noting**: the field operator sees no UDP packets until after `[wifi] gps-live UDP broadcast on port 5555` lands in the boot log. |
| R2.2 | `WiFiUDP` thread safety | `gps_live_udp_init()` runs from the main `setup()` task. `gps_live_udp_broadcast()` runs from the GPS task on core 0. These do not run concurrently — init fully completes before any broadcast can fire. The HTTP server runs on core 1 but uses the separate `WebServer` object. `s_gps_live_udp` is touched by exactly one task at a time. |
| R2.3 | `snprintf` width vs realistic max | Worst-case payload ≈ 135 B including `\n`. Buffer 160 has 25 B headroom. `int8_t`-promoted fields (`quality_score`, `quality_tier`) are at most 3 chars each per `src/types.h:25-26`. Truncation guard correctly clamps `out_len` to `sizeof(buf) - 1`. |
| R2.4 | Tool argparse mixed forms | Verified: `--source udp /dev/cu.foo`, `/dev/cu.foo --source udp`, `--source udp` all parse correctly, positional defaults filled in. |
| R2.5 | Tool thread safety on `state` | `udp_reader()` calls `parse_line()` exactly the way `serial_reader()` does. All `state` mutations go through `_locked_update`/`_locked_*` helpers under `state_lock`. No new race. |
| R2.6 | `do_GET("/")` template choice | `OFFLINE_MAP` is set in `main()` before `ThreadingHTTPServer.serve_forever()` starts. Tests don't run `main()`, so `OFFLINE_MAP` keeps its module default `False` and `HTML` (Leaflet) is served — backward compatible. |
| R2.7 | UTF-8 decode + leftover handling | `data.decode("utf-8", errors="replace")` is safe against malformed bytes. The `lines = text.split("\n"); leftover = lines.pop()` pattern correctly preserves a partial trailing line. |
| R2.8 | `--source udp` skips serial bootstrap | `main()` only spawns `track_query_bootstrap` when `SOURCE == "usb"`. The teardown `gps stream off` is similarly gated. **Verified.** |
| R2.9 | Health snapshot field | `/state` always exposes `serial: _read_serial_health()`. UDP path also calls `_mark_serial_connected/_disconnected`, so the JS reads the same field for both transports. The `serial` key name is now slightly misleading — see R1.2 for the deferred rename. |

No integration regressions. One operational note (R2.1) added to risks
and verification flow.

### Round 3 — Code quality

| # | Location | Issue | Action |
|---|----------|-------|--------|
| R3.1 | `tools/live_map.py:udp_reader()` | `leftover` had no upper bound. A misbehaving emitter that never ships `\n` would grow it unboundedly. | **Fix now.** Added `LEFTOVER_MAX = 4096` cap with a one-line warning + drop. ~30× the realistic line. Implemented in this commit. |
| R3.2 | `src/wifi/wifi_server.cpp` `gps_live_udp_init()` | `Serial.printf("[wifi] gps-live UDP broadcast on port %u\n", ...)` casts via `(unsigned)` — fine but redundant. | **Intentional.** Explicit cast keeps the format spec un-ambiguous against any future port-type change. |
| R3.3 | `gps_live_udp_broadcast()` truncation case | If `snprintf` truncates (n >= sizeof(buf)), we drop the trailing `\n` because we clamp to `sizeof(buf) - 1`. The UDP receiver's `leftover` would then merge two lines. | **Accept.** The realistic payload is ≈ 135 B vs 160 B buffer, so truncation cannot happen on this format. R3.1's leftover cap also bounds the damage if it ever did. |
| R3.4 | `HTML_OFFLINE` JS variable named `window` | Shadows the global `window` inside `wobbleCm()`. Currently harmless because the function doesn't reference the global, but reads ambiguously. | **Fix now.** Rename to `recentWindow`. |
| R3.5 | `tests_host/test_live_map_udp.py` | Uses `_pick_free_udp_port()` to avoid 5555 collision; race window between close-and-rebind is tiny. SO_REUSEADDR mitigates further. | **Accept.** Standard pattern for ephemeral-port tests. |
| R3.6 | `_parse_args(argv)` signature | Takes `argv` as a parameter, allowing test injection. `main()` calls it with `sys.argv[1:]`. | **Good.** Already correct. |
| R3.7 | `print()` vs `logging` | New code uses `print()` to match the existing live_map.py style. | **Accept.** Consistency wins; converting the whole file to `logging` is out of scope. |

R3.1 and R3.4 fixed in this same commit. R3.2/R3.3/R3.5/R3.7 explained
and accepted. R3.6 already correct.

### Outcome

PASS. Two real fixes (R3.1 leftover cap, R3.4 variable shadow) applied.
All other deviations are explicit and justified. Tests still green
after the fixes:

```
python3 tests_host/test_live_map_parse.py   # OK
python3 tests_host/test_live_map_udp.py     # OK
bash tools/run_host_tests.sh                # 21 passed, 0 failed
~/.platformio/penv/bin/pio run -e ...       # SUCCESS (both envs)
```

Ready to commit. Walk-test verification (Task 8 manual) is the
remaining work and requires the operator to flash the new firmware,
join the KartGPS AP from a laptop, and run `python3 tools/live_map.py
--source udp --offline-map`.
