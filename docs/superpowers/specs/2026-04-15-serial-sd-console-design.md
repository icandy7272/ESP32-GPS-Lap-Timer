# Serial SD Console Design

## Summary

This spec adds a small read-only USB serial console so the ESP32-S3 firmware can inspect SD-card content without requiring the Wi-Fi UI.

The console is intentionally narrow:

- accept short line-based commands from `Serial`
- list known SD directories used by the product
- print text-friendly file content for quick inspection
- avoid any write, delete, or rename behavior

## Goals

- Let a developer inspect `tracks/` and `sessions/` over the USB serial port.
- Reuse the existing shared SD/SPI locking rules so debug reads do not race normal storage work.
- Keep the implementation isolated from `main.cpp` so future debug commands can be added without turning `loop()` into a parser blob.
- Make command parsing testable on the host side.

## Non-Goals

- Full shell semantics.
- Binary-safe file transfer over serial.
- Editing or deleting SD files from serial.
- Replacing the existing Wi-Fi file APIs.

## User Experience

The firmware should accept newline-terminated commands on the USB debug serial port at the existing `115200` baud rate.

Supported commands in v1:

- `help`
- `ls tracks`
- `ls sessions`
- `cat tracks/<filename>`
- `cat sessions/<filename>`

Responses should be explicit and easy to skim:

- unknown commands print a short usage hint
- invalid paths are rejected before SD access
- missing files print a readable error
- oversized file reads stop at a fixed debug limit and end with a truncation notice

## Architecture

Use a two-layer split:

- a pure `serial_console` module that parses a single input line and returns a structured action
- a firmware-side serial console runner that polls `Serial`, calls the parser, validates paths, reads SD under `spi_mutex`, and prints responses

This keeps parsing rules host-testable while leaving `SdFat`, `Serial`, and FreeRTOS concerns in firmware code.

## Command Rules

### `help`

Print the supported commands and one-line descriptions.

### `ls <dir>`

Only `tracks` and `sessions` are allowed. Output one entry per line plus a final summary count.

Directory-specific filtering:

- `ls tracks` should show regular files in `tracks/`
- `ls sessions` should show regular files in `sessions/`

### `cat <path>`

Only `tracks/<filename>` and `sessions/<filename>` are allowed.

Safety rules:

- reject absolute paths
- reject `..`
- reject empty filenames
- reject paths outside the two allowed top-level directories

Output rules:

- print a short header with the path
- stream file bytes in chunks
- stop after a fixed maximum, initially `4096` bytes
- if truncated, print `...[TRUNCATED]`

## Error Handling

- Parser errors return a user-facing help string rather than silent failure.
- If `spi_mutex` cannot be acquired quickly, print `ERR: SD busy`.
- If SD open fails, print `ERR: file not found` or `ERR: directory not found`.
- If a file contains binary-looking bytes, still print the captured bytes best-effort; this is a debug console, not a strict text decoder.

## Testing

Host-side tests should cover:

- command parsing for valid `help`, `ls`, and `cat` lines
- trimming of leading/trailing whitespace
- rejection of bad directories and path traversal
- truncation metadata for `cat` requests if that logic lives in the pure module

Firmware verification should cover:

- boot still succeeds with the console compiled in
- `ls tracks` prints the JSON files already present on SD
- `cat tracks/track_001.json` prints the file contents
- invalid commands do not crash or block the loop
