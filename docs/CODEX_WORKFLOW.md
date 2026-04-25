# Codex Workflow

This document records project-specific rules for AI-assisted implementation. It is meant to prevent process decisions from living only in chat context.

## Documentation And Implementation Consistency

Before committing implementation changes, run a documentation consistency pass:

1. Keep implementation plans at the right level of detail. Avoid field-level pseudocode unless the exact fields, function names, log keys, or file names are already confirmed.
2. If a plan, architecture note, TODO, or test README names a concrete symbol, verify that it exists in the implementation or mark it explicitly as `future`, `proposed`, or `deferred`.
3. When implementation changes a planned API, struct, field, log key, file name, or validation command, update the plan and durable docs in the same change.
4. Keep document roles distinct:
   - `docs/superpowers/plans/`: execution history, decisions, checked-off work, and discovered constraints.
   - `docs/ARCHITECTURE.md`: durable facts about the shipped design and current data flow.
   - `TODOS.md`: known follow-up work, hardware validation, and deferred decisions.
   - `tests_host/README.md`: executable host-test coverage and how to run it.
5. Do not describe proposed behavior as shipped behavior. Use clear wording such as `planned`, `future`, `deferred`, or `not implemented yet`.
6. During staged implementation, update docs at task boundaries instead of leaving all doc sync until the final commit.

## Pre-Commit Checklist

For any commit that changes behavior, protocol contracts, public APIs, storage formats, diagnostics, or validation commands:

1. Search the touched docs for concrete identifiers from the change.
2. Search the codebase for each identifier named as current behavior.
3. Fix stale names or mark them as future/proposed before committing.
4. Run the relevant tests and build commands.
5. Include doc changes in the same commit when the implementation changes documented behavior.

Useful checks:

```bash
git diff --check
rg -n "future|proposed|deferred|TODO|GpsFixBundle|match_fix|raw_fix" docs TODOS.md tests_host
bash tools/run_host_tests.sh
~/.platformio/penv/bin/pio run
```

Adjust the `rg` terms to match the symbols changed in the current task.

## Commit Message Notes

If a commit changes architecture or a documented limitation, mention the reason in the commit message body or summary. This keeps future reviews from having to reconstruct why the docs and implementation moved together.
