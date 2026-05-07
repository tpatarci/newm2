---
phase: 05-configuration-system
plan: 02
subsystem: config
tags: [cli, getopt_long, parsing, precedence]
dependency_graph:
  requires: [05-01]
  provides: [cli-parsing]
  affects: [Config.cpp, test_config.cpp]
tech_stack:
  added: [getopt_long, getopt.h]
  patterns: [GNU long options, boolean flag-presence=enable]
key_files:
  created: []
  modified:
    - src/Config.cpp
    - tests/test_config.cpp
decisions:
  - Used std::strcmp for option name matching (C-string comparison avoids string construction per option)
  - Caught std::exception base class for CLI int parsing (simpler than separate invalid_argument/out_of_range catches)
  - Fixed race condition in fork-based exit test by reading pipe after waitpid
metrics:
  duration: 5min
  completed: "2026-05-07"
  tasks: 1
  files: 2
  commits: 3
  tests_added: 16
  tests_total: 35
---

# Phase 5 Plan 2: CLI Argument Parsing Summary

getopt_long CLI parsing with GNU-style long options, boolean flag-presence=enable pattern, and precedence over config file values.

## What Was Done

Implemented `Config::applyCliArgs()` using `getopt_long()` with a 26-entry `struct option` array covering all 18 config settings. Boolean flags use presence=enable (`--auto-raise`) and `--no-xxx` for explicit disable (`--no-auto-raise`). String and integer settings use `--key=value` syntax. Integer values are clamped to the same ranges as config file parsing (thickness 1-50, delays 1-60000). Unknown options cause `exit(2)` with a help suggestion. Wired `Config::load()` to call `applyCliArgs()` after file parsing, completing the precedence chain: defaults -> system config -> user config -> CLI overrides.

## Commits

| Hash | Type | Message |
|------|------|---------|
| 8778cec | test | add failing CLI parsing tests for getopt_long (RED) |
| 199da02 | feat | implement getopt_long CLI parsing in Config::applyCliArgs() (GREEN) |
| f0d0639 | fix | fix race condition in CLI unknown option exit test |

## TDD Gate Compliance

- RED gate: `8778cec` -- 16 failing tests against stub `applyCliArgs()`
- GREEN gate: `199da02` -- all 35 tests pass with full implementation
- REFACTOR gate: `f0d0639` -- test fix for intermittent failure (race condition in fork/pipe)

All three TDD gates present and valid.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed race condition in CLI exit test**
- **Found during:** Task 1 (GREEN phase verification)
- **Issue:** Fork-based unknown option exit test intermittently failed (1/5 runs). Parent read from pipe before child exited, getting empty or partial stderr output.
- **Fix:** Reordered to `waitpid()` first, then `read()` from pipe. Added `setvbuf(stderr, nullptr, _IONBF, 0)` in child for unbuffered stderr.
- **Files modified:** tests/test_config.cpp
- **Commit:** f0d0639

**2. [Rule 2 - Missing] Added `#include <cstring>` for `std::strcmp`**
- **Found during:** Task 1 (implementation)
- **Issue:** Used `std::strcmp` for option name matching but header not included
- **Fix:** Added `#include <cstring>` to Config.cpp
- **Files modified:** src/Config.cpp
- **Commit:** 199da02

## Test Coverage

16 new CLI tests covering:
- String setting override (--tab-foreground=red)
- Integer setting override (--frame-thickness=3)
- Boolean enable (--auto-raise, --click-to-focus, --exec-using-shell)
- Boolean disable (--no-auto-raise, --no-click-to-focus, --no-exec-using-shell)
- Multiple options on same command line
- CLI precedence over config file values
- Unknown option error with exit code 2
- -- terminator stops option parsing
- Integer value clamping (thickness 1-50, delays 1-60000)
- No CLI args leaves config unchanged

## Verification Results

- Config tests: 35/35 passed (123 assertions)
- Full test suite: 88/88 passed
- Stability: 20 consecutive runs all pass

## Self-Check

- [x] src/Config.cpp contains `static struct option longOptions[]` with 26 entries
- [x] src/Config.cpp contains `Config::applyCliArgs()` using `getopt_long()`
- [x] src/Config.cpp wires `Config::load()` to call `applyCliArgs()`
- [x] tests/test_config.cpp contains 16 CLI test cases
- [x] All tests pass (verified with 20 consecutive runs)

## Self-Check: PASSED

All files and commits verified present.
