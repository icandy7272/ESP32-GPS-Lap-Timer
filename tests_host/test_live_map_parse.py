"""Regression tests for `tools/live_map.py` parse hardening.

Codex P1 finding (2026-04-19): the previous `[0-9.\\-]+` regexes admitted
malformed tokens like `..` or `1..2`, which `float()` then raised
`ValueError` on, killing the serial_reader thread and silently freezing
the live map on its last frame.  These tests pin down that:

  1. Tightened regexes reject the malformed cases cleanly.
  2. parse_line() is crash-proof against unexpected input because
     serial_reader wraps it in try/except — but the first line of
     defence must remain the regexes themselves.

Run with: python3 tests_host/test_live_map_parse.py
Exit code: 0 on pass, non-zero on failure (pytest-free on purpose so the
existing tools/run_host_tests.sh loop can pick it up if extended).
"""

from __future__ import annotations

import importlib.util
import pathlib
import sys
from typing import Any


def _load_live_map() -> Any:
    repo_root = pathlib.Path(__file__).resolve().parents[1]
    module_path = repo_root / "tools" / "live_map.py"
    spec = importlib.util.spec_from_file_location("live_map", module_path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    # live_map opens a serial port at import time from the serial_reader
    # thread, but only when its __main__ block runs.  Plain import is
    # safe.  We do need pyserial to be importable — so if it is not,
    # skip gracefully rather than crashing the suite.
    try:
        spec.loader.exec_module(module)
    except SystemExit:
        # live_map calls sys.exit(1) if pyserial is missing.  Skip.
        print("SKIP: pyserial not installed; skipping live_map parse tests")
        sys.exit(0)
    return module


FAIL_COUNT = 0


def check(description: str, got: Any, want: Any) -> None:
    global FAIL_COUNT
    if got != want:
        sys.stderr.write(f"FAIL: {description} — got {got!r}, want {want!r}\n")
        FAIL_COUNT += 1


def main() -> int:
    live_map = _load_live_map()

    # --- Tightened regexes reject malformed numbers --------------------

    re_xing = live_map._XING_CANDIDATE_RE
    re_spread = live_map._DRAFT_SPREAD_RE
    re_latlon = live_map._LATLON_RE
    re_hdop = live_map._HDOP_RE

    # Well-formed baseline: every regex matches a valid example.
    check(
        "candidate: valid input matches",
        bool(re_xing.search(
            "[xing] L0 candidate u=0.475 overshoot=0.00 hdiff=+3.1 "
            "result=PASS reason=segment")),
        True,
    )
    check(
        "spread: valid input matches",
        bool(re_spread.search("spread=0.85m tier=High samples=47")),
        True,
    )
    check(
        "latlon: valid input matches",
        bool(re_latlon.search("lat=40.0059425 lon=116.4584473")),
        True,
    )

    # Malformed cases that killed the reader before: the regex must NOT
    # match, so float() is never called on garbage.
    malformed_candidates = [
        # Two dots in u value.
        "[xing] L0 candidate u=1..2 overshoot=0.0 hdiff=+3 "
        "result=PASS reason=segment",
        # Lone dot.
        "[xing] L0 candidate u=. overshoot=0.0 hdiff=+3 "
        "result=PASS reason=segment",
        # Empty number after `=`.
        "[xing] L0 candidate u= overshoot=0.0 hdiff=+3 "
        "result=PASS reason=segment",
        # Sign-only.
        "[xing] L0 candidate u=- overshoot=0.0 hdiff=+3 "
        "result=PASS reason=segment",
    ]
    for idx, bad in enumerate(malformed_candidates):
        check(
            f"candidate: malformed case {idx} rejected",
            bool(re_xing.search(bad)),
            False,
        )

    check(
        "spread: malformed `spread=..m` rejected",
        bool(re_spread.search("spread=..m tier=High samples=5")),
        False,
    )
    check(
        "latlon: lone-dot lat rejected",
        bool(re_latlon.search("lat=. lon=116.0")),
        False,
    )
    check(
        "hdop: empty value rejected",
        bool(re_hdop.search("hdop=")),
        False,
    )

    # --- parse_line is crash-proof even on surprise input --------------
    #
    # If a future firmware emits something neither the old nor the new
    # regex anticipated, parse_line must not raise — the reader thread
    # depends on that invariant.  We test parse_line directly (the
    # try/except wrapper in serial_reader is a second line of defence,
    # not a replacement).

    surprise_inputs = [
        "",                                      # blank
        "\x00\x01\x02 garbage",                  # partial frame bytes
        "[gps] lat=.. lon=..",                   # malformed numeric fields
        "[draft] p1 = (not_a_number, also_bad)", # wrong shape
        "[xing] L0 candidate u=NaN overshoot=0.0 hdiff=+3 "
        "result=PASS reason=segment",           # float-ish token that our
                                                 # regex rejects
        "random unstructured log line",
        # Truncation mid-line:
        "[xing] L0 candidate u=0.5",
    ]
    for text in surprise_inputs:
        try:
            live_map.parse_line(text)
        except Exception as exc:  # pragma: no cover — test fails below
            check(f"parse_line({text!r}) must not raise", f"raised {exc!r}", "no raise")

    if FAIL_COUNT == 0:
        print("test_live_map_parse: OK")
        return 0
    return FAIL_COUNT


if __name__ == "__main__":
    sys.exit(main())
