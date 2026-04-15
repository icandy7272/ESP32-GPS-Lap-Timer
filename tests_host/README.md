# Host-Side Unit Tests

Standalone unit tests for the project's portable helper modules.
These run on the host machine (no ESP32, no Arduino, no FreeRTOS),
which makes them fast feedback for the pure logic that's been
extracted from the firmware specifically to be testable.

## What's covered

| Test file | Module under test | Why it's portable |
|---|---|---|
| `test_boot_sequence.cpp` | `src/boot_sequence.cpp` | Early boot pin-safe-state, cold-start probe naming/formatting, and stabilization timing helpers — pure data assembly and constant access with no Arduino runtime dependency |
| `test_boot_status.cpp` | `src/boot_status.cpp` | Boot stage/state transitions, ASCII-safe detail storage, and structured serial line formatting over plain enums/buffers |
| `test_boot_presenter.cpp` | `src/boot_presenter.cpp` | Mapping `BootStatus` into the restrained boot splash view model (stage label, detail text, progress segments) with no hardware dependencies |
| `test_track_runtime.cpp` | `src/track_runtime.cpp` | Track lifecycle decisions (boot detection sync, delete-while-recording guard, late auto-detect guard) — pure functions over `TrackDefinition` and primitive types, no Arduino/FreeRTOS deps |
| `test_track_creation_feedback.cpp` | `src/track_creation_feedback.cpp` | Track-save diagnostic mapping and start/finish minimum-separation validation — pure enums, strings, and meter-space geometry helpers |
| `test_storage_naming.cpp` | `src/storage_naming.cpp` | Final session VBO path layout — pure string formatting |
| `test_button_profile.cpp` | `src/button_profile.cpp` | Button event classification and profile lookup logic over plain enums/structs with no hardware dependencies |
| `test_time_format.cpp` | `src/time_format.cpp` | Lap/delta time string formatting using pure integer and buffer logic |
| `test_serial_console.cpp` | `src/serial_console.cpp` | USB serial debug command parsing and SD-path validation, kept pure so command grammar can be verified without Arduino/SdFat |

## Running

```sh
bash tools/run_host_tests.sh
```

The script:
1. Iterates `tests_host/test_*.cpp`
2. Pairs each `test_X.cpp` with `src/X.cpp` (matching by name)
3. Compiles standalone with `c++ -std=c++17 -I src`
4. Runs the resulting binary
5. Reports pass/fail and exits with the failure count

Exit `0` = all green. Non-zero exit = number of failed tests.

## Adding a new host test

1. Extract the testable pure logic into `src/<name>.{h,cpp}` at the
   top level of `src/` (NOT inside a subdirectory like `src/gps/`).
   Pure functions only — no `TFT_eSPI`, no `WiFi.h`, no FreeRTOS
   queues / mutexes, no `Serial`. If you need any of those, the code
   isn't portable enough yet — keep it inside the firmware module.
2. Create `tests_host/test_<name>.cpp` with the same `<name>`.
3. Use plain `<assert.h>` and a standard `int main()` that returns 0
   on success.
4. Run `bash tools/run_host_tests.sh` to verify it picks up and passes.

## Why a shell script and not `pio test`?

PlatformIO's `pio test` workflow expects either Unity or Doctest
framework conventions: `setUp()` / `tearDown()` / `RUN_TEST(...)`
macros, and a particular `test/test_<name>/` directory layout.
The current tests use plain `<assert.h>` and a standard `int main()`,
which is simpler to read and matches how Codex generated them.

A shell script keeps the tests in their native form and avoids a
framework conversion. If you want `pio test` integration later,
the migration path is:
- Move `tests_host/test_X.cpp` → `test/test_X/test_X.cpp`
- Convert `assert(...)` calls to `TEST_ASSERT_*(...)`
- Replace `int main()` with Unity's `setUp` / `tearDown` / `RUN_TEST`
- Add `[env:native]` to `platformio.ini` with `test_framework = unity`
