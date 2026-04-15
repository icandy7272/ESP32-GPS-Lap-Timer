# Serial SD Console Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a small read-only USB serial console that can list SD directories and print file contents from `tracks/` and `sessions/`.

**Architecture:** Extract line parsing and path validation into a small top-level pure module so command behavior can be host-tested first. Then wire a firmware-side polling loop into `main.cpp` that reads newline-terminated commands from `Serial`, uses the parser result to enumerate or open SdFat entries under `spi_mutex`, and prints concise responses back to the debug console.

**Tech Stack:** Arduino, FreeRTOS, SdFat, USB Serial, host-side C++17 `<assert.h>` tests via `bash tools/run_host_tests.sh`

---

## File Structure

**Create**

- `src/serial_console.h`
- `src/serial_console.cpp`
- `tests_host/test_serial_console.cpp`

**Modify**

- `src/main.cpp`
- `tests_host/README.md`

**Why this split**

- `src/serial_console.*` owns the command grammar and input sanitation without depending on Arduino or SdFat.
- `src/main.cpp` remains the runtime orchestrator and only gains a compact polling/execution bridge.
- `tests_host/test_serial_console.cpp` locks in the command behavior before firmware code is added.

### Task 1: Add A Pure Serial Console Parser

**Files:**
- Create: `src/serial_console.h`
- Create: `src/serial_console.cpp`
- Test: `tests_host/test_serial_console.cpp`

- [ ] **Step 1: Write the failing host test**

```cpp
#include <assert.h>
#include <string.h>
#include "serial_console.h"

int main() {
    SerialConsoleCommand cmd = serial_console_parse("help");
    assert(cmd.type == SerialConsoleCommandType::Help);

    cmd = serial_console_parse("  ls sessions  ");
    assert(cmd.type == SerialConsoleCommandType::ListDirectory);
    assert(strcmp(cmd.arg, "sessions") == 0);

    cmd = serial_console_parse("cat tracks/track_001.json");
    assert(cmd.type == SerialConsoleCommandType::CatFile);
    assert(strcmp(cmd.arg, "tracks/track_001.json") == 0);

    cmd = serial_console_parse("cat ../secret");
    assert(cmd.type == SerialConsoleCommandType::Invalid);
    return 0;
}
```

- [ ] **Step 2: Run the host test to verify it fails**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -I src -o /tmp/test_serial_console tests_host/test_serial_console.cpp src/serial_console.cpp
```

Expected: compile failure because the module does not exist yet.

- [ ] **Step 3: Implement the minimal parser**

Create a small API like:

```cpp
enum class SerialConsoleCommandType {
    Invalid,
    Help,
    ListDirectory,
    CatFile,
};

struct SerialConsoleCommand {
    SerialConsoleCommandType type;
    char arg[128];
    char error[96];
};

SerialConsoleCommand serial_console_parse(const char* line);
```

Rules:

- trim leading and trailing whitespace
- accept only `help`, `ls tracks`, `ls sessions`, and `cat <allowed-path>`
- reject empty filenames, `..`, absolute paths, and directories other than `tracks` / `sessions`

- [ ] **Step 4: Re-run the host test to verify it passes**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -I src -o /tmp/test_serial_console tests_host/test_serial_console.cpp src/serial_console.cpp && /tmp/test_serial_console
```

Expected: exit `0`

- [ ] **Step 5: Run the full host test suite**

Run:

```bash
bash tools/run_host_tests.sh
```

Expected: all host tests plus `test_serial_console` pass.

### Task 2: Wire The Parser Into The Firmware Serial Loop

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add a small line buffer and polling helper**

Add helpers in `src/main.cpp` that:

- accumulate bytes from `Serial.read()`
- trigger on `\n` or `\r`
- ignore empty lines
- call the pure parser

- [ ] **Step 2: Implement `help` output**

Print the supported commands and brief descriptions.

- [ ] **Step 3: Implement `ls` execution**

Using `spi_mutex` and `SdFat`:

- open `tracks/` or `sessions/`
- iterate regular files
- print one file per line
- print a final `count=<n>` summary

- [ ] **Step 4: Implement `cat` execution**

Using `spi_mutex` and `SdFat`:

- open only validated `tracks/<filename>` or `sessions/<filename>`
- print a header
- stream bytes in chunks
- stop after `4096` bytes
- print `...[TRUNCATED]` if the file is larger

- [ ] **Step 5: Call the polling helper from `loop()`**

Keep the existing stack-watermark snapshot behavior intact.

- [ ] **Step 6: Run firmware-focused verification**

Run:

```bash
bash tools/run_host_tests.sh
```

If a full PlatformIO build is available locally, also run:

```bash
pio run
```

Expected:

- host tests pass
- firmware compiles cleanly if `pio` is available

### Task 3: Update Test Documentation

**Files:**
- Modify: `tests_host/README.md`

- [ ] **Step 1: Document the new host-tested module**

Add `test_serial_console.cpp` to the coverage table and note that the parser is intentionally pure so command grammar can be verified without hardware.

- [ ] **Step 2: Re-run the host test suite**

Run:

```bash
bash tools/run_host_tests.sh
```

Expected: all tests pass.
