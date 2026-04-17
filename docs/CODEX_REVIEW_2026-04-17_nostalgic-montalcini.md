# Codex Review Memo: `claude/nostalgic-montalcini` (2026-04-17)

This memo supersedes the 2026-04-16 review.

Reviewed against:

- `main...HEAD` on `claude/nostalgic-montalcini`
- current worktree contents under `.claude/worktrees/nostalgic-montalcini`

No source changes were made as part of this review. This is a review-only handoff.

## Verification Performed

Commands run on the branch worktree:

- `bash tools/run_host_tests.sh`
- `~/.platformio/penv/bin/pio run`
- `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1-walking-test`

Results:

- host tests: `8 passed, 0 failed`
- production build: `SUCCESS`
- walking-test build: `SUCCESS`

So the branch is buildable in both firmware environments. The remaining concern is documentation consistency, not compilation.

## Previously Reported Items Now Closed

The following earlier findings appear resolved in the current branch state:

1. `READY` screen stale timer after stop
2. Lap-timer/session split-brain on the 5 s walking-test threshold
3. Walking-test behavior enabled in the default firmware build
4. `PRD` / `ARCHITECTURE` still modeling PPS as normal `v1` behavior
5. `TEST_MODES.md` deployment checklist overstating the checked-in repo settings

## Findings

### 1. [P1] Builder docs still describe an optional PPS fly-wire path, but the latest product docs now say no PPS path exists at all

Files:

- `docs/WIRING.md:115`
- `docs/WIRING.md:129`
- `docs/WIRING.md:262-263`
- `docs/WIRING.md:364`
- `pcb-guide.md:144`
- `pcb-guide.md:155`
- `pcb-guide.md:185`
- `pcb-guide.md:643-649`

The latest doc pass took a stronger position in `docs/PRD.md` and `docs/ARCHITECTURE.md`: BK-880 does not expose PPS and there is no usable fly-wire test pad either, so the timing model is purely UART-arrival-time based.

But the hardware-builder docs still tell a different story:

- `docs/WIRING.md` says PPS is not on the 6P harness, but can still be fly-wired from a module test pad to `GPIO16`
- `pcb-guide.md` still tells the builder to reserve a PPS pad / test point and discusses adding a future fly-wire

That leaves two incompatible hardware stories in the same branch:

- `PRD` / `ARCHITECTURE`: no PPS path exists
- `WIRING` / `pcb-guide`: PPS is optional via fly-wire

This is still a merge blocker because it changes how someone would wire the device and what they would reserve on a PCB or perfboard.

Recommended resolution:

- if the newer stronger claim is correct, remove the PPS fly-wire / test-pad instructions from `docs/WIRING.md` and `pcb-guide.md`
- otherwise, soften `PRD` / `ARCHITECTURE` back to the optional-fly-wire story and keep one consistent hardware narrative everywhere

## Non-Blocking Note

Some source comments and identifiers still talk about PPS as a live path:

- `src/gps.h`
- `src/types.h`
- `src/pins.h`
- `src/gps/gps_fix.cpp`
- `src/gps/gps_task.cpp`

I am not treating that as a merge blocker right now because the branch still builds and the current blocking issue is the user-facing hardware documentation. But after the final PPS story is settled, those comments should be cleaned up too.

## Bottom Line

I do not see any current runtime/code blocker in this branch revision. The remaining blocker is singular:

- the latest `PRD` / `ARCHITECTURE` now say BK-880 has no PPS path at all
- `WIRING.md` and `pcb-guide.md` still describe an optional PPS fly-wire / test-pad path

Until those builder docs are reconciled, the branch still does not provide one single source of truth for the hardware.
