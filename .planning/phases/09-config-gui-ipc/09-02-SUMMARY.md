---
phase: 09-config-gui-ipc
plan: 02
subsystem: api
tags: [ndjson, ipc, protocol, config-file, atomic-write, catch2, posix]

requires:
  - phase: 09-config-gui-ipc
    provides: "09-01's `tab-font` and `menu-font` keys, which the managed-key list and the round-trip guard both assert"
  - phase: 08.5-v1.0-closeout
    provides: "D-8.5-01 (a name chosen before v1.0 is permanent, no aliases) -- the rule that makes DISC-01 a one-way door; the house pattern of a display-free header with its own Catch2 target (include/MenuPaint.h, include/EventPump.h)"
  - phase: 07-app-discovery
    provides: "src/AppCache.cpp's hand-rolled JSON escape/unescape precedent, and include/AppEntry.h -- the three-field row shape the menu-entry block serialises back to"
provides:
  - "`include/ConfigProtocol.h`: the frozen version-1 wire contract as one header the WM, wm2-ctl, wm2-config and a Catch2 case all compile identically"
  - "`kConfigProtocolVersion` (1) and `kConfigProtocolMaxLine` (4096)"
  - "`ConfigMessageType` (eleven frozen spellings plus `Unknown`), `ConfigMessage`, `ConfigDecodeResult`, `configProtocolEncode()`, `configProtocolDecode()`, `configMessageTypeName()`, `configMessageTypeFromName()`"
  - "`include/ConfigFileWriter.h` / `src/ConfigFileWriter.cpp`: the surgical, atomic config-file writer (D-02, D-13, D-04)"
  - "`ConfigEdit`, `ConfigWriteResult`, `configFileManagedKeys()` (21 keys), `configFileKeyIsManaged()`, `configFileWrite()`"
  - "ctest labels `config_protocol` (25 cases) and `config_writer` (31 cases), both runnable with no X server"
  - "DISC-01 on the record: the socket path spelling and the `_WM2_CONFIG_SOCKET` property name, frozen here and built in 09-03"
affects: [09-03-socket-server, 09-04-wm2-ctl, 09-05-live-apply, 09-06-menu-page, 09-07-protocol-client, 09-08-gui, 09-09-release]

actuals:
  tokens: 28000     # chars/4 over the realized diff (112,065 chars, 726396a..0cfde8f)
  tasks: 4          # the checkpoint plus three build tasks
  commits: 5

tech-stack:
  added: []
  patterns:
    - "A frozen wire contract is documented AT the header that implements it, not only in planning artifacts -- a client that reads the header knows the whole of what it may rely on"
    - "Atomic file replacement: mkstemp in the TARGET'S OWN DIRECTORY, write, fsync, fchmod to the target's mode, rename; every failure path unlinks the temporary and returns a named enumerator"
    - "A surgical text editor proves preservation by asserting EXACT BYTES of the untouched lines, never by re-parsing -- a re-parse passes against a writer that dropped every comment"
    - "A writer/parser pair is proven inverse by a distinctive-value table whose every value is asserted to differ from the built-in default"
    - "For a compiled language, a declarations-only header in the RED commit turns the RED from a missing-header compile error into an undefined-reference link error, which keeps the RED commit compilable"

key-files:
  created:
    - include/ConfigProtocol.h
    - include/ConfigFileWriter.h
    - src/ConfigFileWriter.cpp
    - tests/test_config_protocol.cpp
    - tests/test_config_writer.cpp
  modified:
    - CMakeLists.txt

key-decisions:
  - "DISC-01: the version-1 wire contract is frozen at the `minimal` option -- eleven message types, one JSON object per line bounded at 4096 bytes, socket at $XDG_RUNTIME_DIR/wm2-born-again/socket<display> with the /tmp/wm2-born-again-<uid> fallback, root property _WM2_CONFIG_SOCKET as XA_STRING"
  - "DISC-01c: an unknown protocol MEMBER name is Malformed, while an unknown TYPE name is the named UnknownType verdict"
  - "DISC-01d: the window-manager version travels in status-reply's field list, not as a member of hello-ack"
  - "status-reply's field list is one FLAT array of alternating strings, so the grammar stays one level deep and the parser needs no recursion"
  - "configFileManagedKeys() is 21 keys; rule-* and menu-entry-* are deliberately outside it"
  - "A duplicated managed key collapses to one line, because Config::applyFile() is later-wins and leaving the duplicate would silently override the save"
  - "A new config file keeps mkstemp's 0600; an existing one keeps its own mode"

patterns-established:
  - "Named-enumerator-for-every-branch (the MenuPaint.h `Foreign` rule) applied to a wire parser: four decode verdicts, no path returning Ok with an unset type, no else-less fallthrough"
  - "Fail-closed refusals validated BEFORE any file is opened, so a refused edit set cannot leave a half-written file"

requirements-completed: [CGUI-02, CGUI-03]

coverage:
  - id: D1
    description: "Every message the version-1 protocol carries encodes to one newline-terminated line and decodes back to the same values, with no display, no X server, no GTK and no window manager running"
    requirement: CGUI-02
    verification:
      - kind: unit
        ref: "tests/test_config_protocol.cpp#Every message type round-trips the fields it carries"
        status: pass
      - kind: unit
        ref: "tests/test_config_protocol.cpp#A hello round-trips its program name and protocol number"
        status: pass
      - kind: unit
        ref: "tests/test_config_protocol.cpp#A value containing a quote, a backslash, a newline and a tab survives a round trip"
        status: pass
      - kind: unit
        ref: "tests/test_config_protocol.cpp#Every byte below 0x20 survives a round trip"
        status: pass
      - kind: integration
        ref: "env -u DISPLAY -u XAUTHORITY ./build/debug/test_config_protocol  # 249 assertions in 25 cases, all passed"
        status: pass
      - kind: integration
        ref: "ldd build/debug/test_config_protocol | grep -c -e libX11 -e libXft -e libgtk  # => 0"
        status: pass
    human_judgment: false
  - id: D2
    description: "A line longer than the declared bound is rejected as too long before any parsing is attempted, and the decoder never grows its buffer to accommodate it (T-9-05)"
    requirement: CGUI-02
    verification:
      - kind: unit
        ref: "tests/test_config_protocol.cpp#A line at exactly the bound decodes and one byte over is rejected as too long"
        status: pass
      - kind: unit
        ref: "tests/test_config_protocol.cpp#The length bound is checked before any scan of the content"
        status: pass
    human_judgment: false
  - id: D3
    description: "A decoded message whose type this version does not speak resolves to a named enumerator, never to a silent fallthrough; nested and adversarial input is a verdict rather than a crash (T-9-06)"
    requirement: CGUI-02
    verification:
      - kind: unit
        ref: "tests/test_config_protocol.cpp#An object whose type this version does not speak resolves to UnknownType"
        status: pass
      - kind: unit
        ref: "tests/test_config_protocol.cpp#Nested objects and arrays are rejected as malformed rather than descended into"
        status: pass
      - kind: unit
        ref: "tests/test_config_protocol.cpp#A line that is not an object is rejected as malformed"
        status: pass
      - kind: unit
        ref: "tests/test_config_protocol.cpp#An object with no type member is rejected as malformed"
        status: pass
      - kind: e2e
        ref: "bash scripts/gates/build-all.sh debug  # the ASan tree exercises the parser; build-all OK: debug"
        status: pass
    human_judgment: false
  - id: D4
    description: "Saving a config file replaces or appends only the lines for keys the writer manages; every comment, blank line, rule-* group and unknown key is byte-for-byte where it was (D-02)"
    requirement: CGUI-03
    verification:
      - kind: unit
        ref: "tests/test_config_writer.cpp#A comment, a blank line, an unknown key and a rule group survive a save byte for byte"
        status: pass
      - kind: unit
        ref: "tests/test_config_writer.cpp#A managed key is replaced in place, keeping its position and its spacing"
        status: pass
      - kind: unit
        ref: "tests/test_config_writer.cpp#An edit naming an unmanaged key is refused and the file is not touched"
        status: pass
      - kind: unit
        ref: "tests/test_config_writer.cpp#A rule-* key touches no managed field"
        status: pass
    human_judgment: false
  - id: D5
    description: "Saving is atomic: a failure part-way through leaves the original file exactly as it was, because the new text is written to a temporary file in the same directory and renamed over the original (T-9-07, T-9-08, T-9-10)"
    requirement: CGUI-03
    verification:
      - kind: unit
        ref: "tests/test_config_writer.cpp#A temporary that cannot be created in the target's own directory fails the save and leaves the original untouched"
        status: pass
      - kind: unit
        ref: "tests/test_config_writer.cpp#A save leaves no file anywhere but the target and its own directory"
        status: pass
      - kind: integration
        ref: "grep -c 'rename(' src/ConfigFileWriter.cpp  # => 2"
        status: pass
    human_judgment: false
  - id: D6
    description: "Removing a key deletes its line from the user file rather than writing the built-in default, so a value set only in the system-wide layer shows through again (D-13)"
    requirement: CGUI-03
    verification:
      - kind: unit
        ref: "tests/test_config_writer.cpp#A removal deletes the line and never writes the built-in default"
        status: pass
      - kind: unit
        ref: "tests/test_config_writer.cpp#A removal wins over a value left in the same edit"
        status: pass
      - kind: unit
        ref: "tests/test_config_writer.cpp#Removing a key the file does not contain is a no-op, not an append"
        status: pass
    human_judgment: false
  - id: D7
    description: "Writing the same edit set twice over the same input file produces byte-identical output the second time"
    requirement: CGUI-03
    verification:
      - kind: unit
        ref: "tests/test_config_writer.cpp#Running the same edit set twice produces byte-identical output"
        status: pass
      - kind: unit
        ref: "tests/test_config_writer.cpp#A managed key the file does not contain is appended under one section comment"
        status: pass
    human_judgment: false
  - id: D8
    description: "The writer and the parser are inverses over the key set the GUI owns, and the boundary of that key set is asserted rather than assumed"
    requirement: CGUI-03
    verification:
      - kind: unit
        ref: "tests/test_config_writer.cpp#Every managed key survives a write and a read back through Config::applyFile"
        status: pass
      - kind: unit
        ref: "tests/test_config_writer.cpp#Every managed key reaches a Config field through Config::applyKeyValue"
        status: pass
      - kind: unit
        ref: "tests/test_config_writer.cpp#The managed key list is exactly the twenty-one keys the GUI owns"
        status: pass
      - kind: unit
        ref: "tests/test_config_writer.cpp#Menu entries survive a write and a read back through Config::applyFile"
        status: pass
    human_judgment: false
  - id: D9
    description: "The version-1 wire contract is a decision on the record, taken by a human before the first byte of it was written (DISC-01)"
    verification:
      - kind: manual_procedural
        ref: "operator reply `minimal` at the 09-02 checkpoint, 2026-09-06, recorded in include/ConfigProtocol.h's DISC-01 block and in .planning/STATE.md"
        status: pass
    human_judgment: true
    rationale: "The checkpoint's whole purpose was a human judgment about what version 1 commits to forever. A test can assert the eleven spellings are what they are -- and one does -- but nothing automated can confirm the operator's choice was recorded faithfully, that the socket-path and property-name half of the contract is what they intended, or that the DISC-01c/DISC-01d sub-decisions taken during implementation are ones they would have taken. A human has to read the header's DISC-01 block."

duration: 35 min
completed: 2026-09-06
status: complete
---

# Phase 9 Plan 02: The display-free halves Summary

**A frozen, hand-rolled NDJSON codec (`include/ConfigProtocol.h`, header-only, eleven message types, 4096-byte bound checked before any scan) and a surgical atomic config-file writer (`src/ConfigFileWriter.cpp`, 21 managed keys, mkstemp-beside-target then rename) — 56 Catch2 cases across two test binaries that link no display library and run with `DISPLAY` unset.**

## Performance

- **Duration:** 35 min
- **Started:** 2026-09-06T07:10:00Z (approx; first commit 07:26:09Z)
- **Completed:** 2026-09-06T07:45:00Z
- **Tasks:** 4 (one decision checkpoint, three build tasks)
- **Files modified:** 6 (5 created, 1 modified)

## Accomplishments

- **The one-way door is shut, deliberately and on the record.** DISC-01 was decided by the operator at the checkpoint — option `minimal` — **before the first byte of the codec was written**, which is the entire reason the checkpoint existed. The eleven frozen spellings are asserted as literals in `tests/test_config_protocol.cpp`, so a later rename fails a test instead of quietly becoming a breaking change for someone's shell script.
- **The contract lives in the header, not only in planning artifacts.** `include/ConfigProtocol.h` opens with a DISC-01 block naming all four frozen things: the eleven message types, the framing and its bound, the socket path spelling with its `/tmp` fallback, and `_WM2_CONFIG_SOCKET`. The last two are built in 09-03 but frozen here, so a client that reads only this header knows the whole of what it may rely on.
- **The codec has no recursion at all.** T-9-06 asks that deeply nested input not exhaust anything; the grammar is one level deep by construction, so a 500-deep nested object comes back `Malformed` in constant stack. The status-reply field list is a **flat** array of alternating strings for exactly this reason — an array of pairs or of objects would have put a second level in the grammar.
- **The length bound is provably checked first.** Not merely "the check is on line 1": a case feeds content that is *both* unambiguously malformed *and* over the bound, and asserts `TooLong`. If the bound were checked after parsing, that input would come back `Malformed`. That is the observable proof of the ordering, and `TooLong` is asserted as distinct from `Malformed` rather than merely as non-`Ok`.
- **Preservation is asserted in bytes, never by re-parsing.** Every `[config_writer]` preservation case compares the whole resulting file against an exact expected string. A re-parse would pass just as happily against a writer that dropped every comment and blank line in the file, which is precisely the failure D-02 forbids.
- **Atomicity is proven by a real failure, not by inspection.** The T-9-07/T-9-08 case removes write permission from the target's directory **and nowhere else**. A writer that put its temporary in `/tmp` or in the cwd would succeed; this one cannot even create the temporary, returns `TempFailed`, and leaves the original file byte-identical with nothing beside it. One case proves both that the temporary is beside the target and that a part-way failure is not a partial write.
- **The writer and the parser are proven inverses.** A distinctive-value table — every value asserted to differ from its built-in default, or the round trip would pass against a writer that produced nothing — is written through `configFileWrite()`, read back through `Config::applyFile()`, and compared field by field. The drift guard goes further than the plan asked: applying each key through `Config::applyKeyValue()` must change **that** field **and no other**, so a key wired to the wrong member fails too.
- **Zero new compiler warnings**, and the full `build-all.sh debug` gate is green: `build-all OK: debug` (241 s, whole suite).

## Task Commits

1. **Checkpoint (Task 0): freeze the version-1 wire contract** — operator decision `minimal`, no commit of its own; recorded in `include/ConfigProtocol.h`'s DISC-01 block (`87fb868`) and in `.planning/STATE.md`.
2. **Task 1: the codec** (tracer, tdd)
   - `a8d88f9` (test) — 21 failing `[config_protocol]` cases + the display-free CMake target
   - `87fb868` (feat) — `include/ConfigProtocol.h`
3. **Task 2: the surgical writer** (tdd)
   - `ebb2769` (test) — 24 failing `[config_writer]` cases + declarations-only header
   - `63b8b2a` (feat) — `src/ConfigFileWriter.cpp`, plus two guard cases added during GREEN
4. **Task 3: the writer and the parser agree**
   - `0cfde8f` (test) — round trip, drift guard, and `src/Config.cpp` linked into the target

_TDD tasks carry two commits each (test → feat). No refactor commit was needed on either: the codec is a single-pass parser with nothing to extract, and the writer's shape was settled by the read-classify-emit structure the plan named._

## Files Created/Modified

- `include/ConfigProtocol.h` — header-only codec. Includes `<cstddef>`, `<string>`, `<utility>`, `<vector>` and **nothing else** — no X11, no Xft, no GTK, no GLib, no project header.
- `include/ConfigFileWriter.h` — the writer's contract. Includes `AppEntry.h` (plain data, no X11) and the standard library.
- `src/ConfigFileWriter.cpp` — read, classify, emit, rename. `trim()` duplicated from `src/Config.cpp` per the per-translation-unit convention, deliberately so the writer classifies a line exactly as the parser does.
- `tests/test_config_protocol.cpp` — 25 `[config_protocol]` cases, 249 assertions.
- `tests/test_config_writer.cpp` — 31 `[config_writer]` cases, 189 assertions.
- `CMakeLists.txt` — two new targets, both with `ADD_TAGS_AS_LABELS` and neither with X11, Xft or a display fixture.

## Verification Run

| Check | Result |
|---|---|
| `ctest -L '^config_protocol$' --no-tests=error` | 25 tests, all pass |
| `ctest -L '^config_writer$' --no-tests=error` | 31 tests, all pass |
| `ctest -L '^(config\|config_writer\|config_protocol)$' --no-tests=error` | 113 tests, all pass (config 57 + writer 31 + protocol 25) |
| `ldd build/debug/test_config_protocol \| grep -c -e libX11 -e libXft -e libgtk` | `0` |
| `ldd build/debug/test_config_writer \| grep -c -e libX11 -e libXft -e libgtk` | `0` |
| `env -u DISPLAY -u XAUTHORITY ./build/debug/test_config_protocol` | 249 assertions, 25 cases, passed |
| `env -u DISPLAY -u XAUTHORITY ./build/debug/test_config_writer` | 189 assertions, 31 cases, passed |
| `grep -c '^#pragma once' include/ConfigProtocol.h` | `1` |
| forbidden includes (X11/Xft/GTK/GLib/project) in `ConfigProtocol.h` | `0` |
| `grep -c 'enum class ConfigMessageType' include/ConfigProtocol.h` | `1` |
| `grep -c 'enum class ConfigDecodeResult' include/ConfigProtocol.h` | `1` |
| `grep -c 'rename(' src/ConfigFileWriter.cpp` | `2` |
| new compiler/linker warnings in `build/debug/build.log` | `0` |
| `bash scripts/gates/build-all.sh debug` | `build-all OK: debug` (241 s, whole suite) |

Both new binaries were additionally run with `DISPLAY` and `XAUTHORITY` unset from the environment, not merely audited with `ldd` — the plan's claim is that they are runnable on a host with no X server, and the only way to observe that is to remove the display and run them.

## Decisions Made

- **DISC-01 — `minimal`** (operator, at the checkpoint). Eleven message types, framing at one JSON object per newline-terminated line, 4096-byte bound, `$XDG_RUNTIME_DIR/wm2-born-again/socket<display>` with the `/tmp/wm2-born-again-<uid>` fallback, `_WM2_CONFIG_SOCKET` as `XA_STRING`. `minimal-plus-capabilities` and `defer` were declined. Permanent under D-8.5-01.
- **DISC-01c — an unknown MEMBER name is `Malformed`; an unknown TYPE name is `UnknownType`.** The asymmetry is the point. A v1 peer that quietly ignored an unknown member would accept a v2 `set` while dropping the qualifier that changed its meaning — it would act on a message it did not understand. An unknown type it cannot act on at all, so naming it and declining is safe, and is what lets a later version add a message additively. The protocol number in the hello is how a peer learns what it may send.
- **DISC-01d — the WM version travels in `status-reply`'s field list, not in `hello-ack`.** D-15 asks the handshake for a program name and a protocol version and nothing else; D-14 already names the WM version as status. One fewer member is one fewer thing frozen forever. This deliberately departs from the illustrative `"version":"0.1.0"` in 09-RESEARCH.md's Code Examples, which is a sketch rather than a locked decision.
- **`status-reply`'s field list is one flat array of alternating strings.** Preserves order (JSON object member order does not), and keeps the grammar strictly one level deep so the parser needs no recursion — which is what makes T-9-06 structural rather than defended.
- **An `Unknown` message is not encodable** and `configProtocolEncode()` returns `""` for it. A valid line always ends in `\n`, so the empty string is never a valid line and a caller who fails to check cannot put a half-message on the wire.
- **`configFileManagedKeys()` is 21 keys; `rule-*` and `menu-entry-*` are outside it.** Rule pass-through is what lets the deferred rules editor land later with no file-format work. `menu-entry-*` is an ordered three-line group rather than an independent setting, so it is rewritten as a block through the `menuEntries` parameter instead.
- **A duplicated managed key collapses to one line.** `Config::applyFile()` is later-wins, so replacing only the first occurrence would leave a later line overriding the value the user just chose — the save would appear to do nothing. Collapsing is within what the writer owns, since every line involved is a managed key.
- **Replacement preserves the line's existing spacing** (indentation, the key's own spelling, the `=`, and the whitespace after it). A courtesy, and also what makes a second identical save byte-identical for free rather than by special-casing.
- **A new config file keeps `mkstemp`'s 0600; an existing one keeps its own mode.** More restrictive than a text editor would produce, never a problem (the WM runs as the same uid), and in the spirit of D-16's boundary.
- **A declarations-only header ships in Task 2's RED commit,** so the RED is an undefined-reference *link* error rather than a missing-header *compile* error. 09-01 recorded the compile-error RED as a `git bisect` hazard; this keeps the RED commit compilable and proves the cases compile against the declared contract.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking] The plan's `<behavior>` asks for three distinct decode results where its `<action>` fixes an enum with two usable ones**

- **Found during:** Task 1
- **Issue:** The behaviour clause requires that "a line that is not an object, an object with no type, and an object whose type is a string the protocol does not know each resolve to a **distinct** named result". The action clause and the artifacts table both fix `enum class ConfigDecodeResult { Ok, TooLong, Malformed, UnknownType }` — four enumerators, of which `TooLong` is spoken for by the length case, leaving two for three structural outcomes. The two statements cannot both be satisfied.
- **Fix:** Resolved in favour of the enum, which the plan states **twice** (action clause and artifacts table) against the behaviour clause's once. Non-object and missing-type both return `Malformed`; unknown type returns `UnknownType`. The distinction that carries weight is preserved and asserted: `UnknownType` is checked to be neither `Malformed` nor `Ok`, because that is the distinction a later protocol version depends on — a v1 peer must be able to tell "a message I do not speak" from "bytes I could not parse". Three cases assert the three inputs, each producing a non-`Ok` result, with the unknown-type one distinguishable.
- **Files modified:** `include/ConfigProtocol.h`, `tests/test_config_protocol.cpp`
- **Verification:** `A line that is not an object is rejected as malformed`, `An object with no type member is rejected as malformed`, `An object whose type this version does not speak resolves to UnknownType` — all green.
- **Committed in:** `a8d88f9` / `87fb868`

**2. [Rule 2 — Missing Critical] `ConfigWriteResult` had no arm for a refused edit, so the plan's own prohibition was unenforceable**

- **Found during:** Task 2
- **Issue:** The plan's `must_haves.prohibitions` state that the save "must never remove, reorder or rewrite a line the writer does not manage", but the enumerated `ConfigWriteResult { Ok, ReadFailed, DirectoryFailed, TempFailed, WriteFailed, RenameFailed }` is entirely I/O failures. With no arm for "the caller asked for something forbidden", a `set` naming `rule-match-class` — from a buggy GUI, or from a value that arrived over the socket — would have had to be either silently ignored or silently honoured. Silently honoured rewrites a rule line, which is the prohibition. Silently ignored is a save that reports success and did not save.
- **Fix:** Added `ConfigWriteResult::InvalidEdit`, checked **before any file is opened** so a refusal cannot leave a half-written file. Unlike the codec's enum, this one is enumerated only once in the plan (the artifacts table names the type without its values), so extending it does not contradict a twice-stated list. It covers four refusals, all fail-closed: an unmanaged key, a duplicate key within one edit set, a line break in a key or value, and a value over the 256 bytes `Config::applyFile()` will read back.
- **Files modified:** `include/ConfigFileWriter.h`, `src/ConfigFileWriter.cpp`, `tests/test_config_writer.cpp`
- **Verification:** `An edit naming an unmanaged key is refused and the file is not touched` (four sub-assertions, including that a valid edit beside an invalid one does **not** land on its own), `A value carrying a newline is refused rather than injected as an extra line`, `A value longer than the parser will read back is refused`, `A menu entry carrying a newline is refused`, `Two edits naming the same key are refused rather than half-honoured`.
- **Committed in:** `ebb2769` / `63b8b2a`

**3. [Rule 2 — Missing Critical] A line-break in a value is a config-file line injection**

- **Found during:** Task 2 (folded into deviation 2's enumerator, listed separately because it is a different defect)
- **Issue:** Values reaching `configFileWrite()` come from a GUI form and, in later plans, from the socket. A value containing `\n` would have been written verbatim, and the next line of the user's config file would be whatever the sender chose — `rule-match-class = Anything`, for instance. Nothing in the plan names this; the threat register's T-9-08 covers paths, not content.
- **Fix:** `\n` and `\r` in any key, value, menu-entry name, command or category are refused. Note this is the writer's *mirror* of the codec's rule that a raw control character inside a JSON string literal is `Malformed` — the framing assumption "one record per line" is enforced at both ends.
- **Files modified:** `src/ConfigFileWriter.cpp`, `tests/test_config_writer.cpp`
- **Verification:** the two newline-refusal cases above.
- **Committed in:** `ebb2769` / `63b8b2a`

**4. [Rule 1 — Bug] Replacing only the first occurrence of a duplicated managed key would have made the save a no-op**

- **Found during:** Task 2
- **Issue:** `Config::applyFile()` is later-wins. A user file containing `frame-thickness = 7` … `frame-thickness = 15` and a save of `9` would, under a naive replace-in-place, produce `frame-thickness = 9` … `frame-thickness = 15` — and the window manager would read 15. The GUI would show 9, the WM would use 15, and no test in the plan's list would have caught it.
- **Fix:** The first occurrence is replaced in place (keeping its position); subsequent occurrences of that same managed key are dropped.
- **Files modified:** `src/ConfigFileWriter.cpp`, `tests/test_config_writer.cpp`
- **Verification:** `A duplicated managed key collapses to the one line the parser will read` — asserts the exact resulting bytes, including that the comment between the two occurrences survives.
- **Committed in:** `ebb2769` / `63b8b2a`

**5. [Rule 1 — Bug] The trailing-newline predicate matched "one line removed and one appended"**

- **Found during:** Task 2, self-review before committing GREEN
- **Issue:** The first implementation preserved a missing final newline when `out.size() == lines.size()`. That is a count, and a count also holds when a save removed one line and appended another — in which case the last line is a *different* line and truncating its newline is wrong.
- **Fix:** The predicate is now the last line itself (`out.back() == lines.back()`), which is what the rule actually means.
- **Files modified:** `src/ConfigFileWriter.cpp`, `tests/test_config_writer.cpp`
- **Verification:** `A file with no trailing newline keeps that shape when nothing lands at its end` and `A file that does not end in a newline gains one before an appended key` — the two sides of the predicate.
- **Committed in:** `63b8b2a`

**6. [Rule 3 — Blocking] The test helper `remove()` resolved to `std::remove()` and broke the RED build for the wrong reason**

- **Found during:** Task 2 RED
- **Issue:** A file-local helper named `remove()` in an anonymous namespace lost overload resolution to `std::remove(const char*)` from `<cstdio>`, so `{remove("frame-thickness")}` was an `{int}` braced list and the compiler tried the `vector(size_type)` constructor. The RED failed with a confusing conversion error instead of the undefined reference the task was waiting for.
- **Fix:** Renamed to `removeKey()`.
- **Files modified:** `tests/test_config_writer.cpp`
- **Verification:** the RED then failed exactly as intended — four undefined references and nothing else.
- **Committed in:** `ebb2769`

**7. [Rule 2 — Missing Critical] The drift guard asserts that no OTHER field moved**

- **Found during:** Task 3
- **Issue:** The plan asks only that applying a managed key "must change some field". A key mistakenly wired to the wrong `Config` member — `menu-highlight` setting `menuBackground`, say — passes that assertion and ships a control that visibly does the wrong thing.
- **Fix:** The guard snapshots all 21 managed fields, applies one key, and asserts that field changed **to the value written** and every other field is unchanged.
- **Files modified:** `tests/test_config_writer.cpp`
- **Verification:** `Every managed key reaches a Config field through Config::applyKeyValue` — 21 keys × 21 field comparisons, all green.
- **Committed in:** `0cfde8f`

---

**Total deviations:** 7 auto-fixed (4 missing critical, 2 blocking, 2 bugs — deviation 3 is counted once under missing-critical though it is also a security fix). No Rule 4 architectural decisions arose beyond the checkpoint the plan already scheduled.
**Impact on plan:** Deviations 1 and 2 resolve genuine internal contradictions in the plan and are recorded so a reviewer can disagree with the resolution. Deviations 3–5 and 7 are correctness and security work the plan's prose commits to but its criteria omit. Deviation 6 is a test-authoring mistake, fixed in the same commit it was made. No file outside the plan's `files_modified` was touched.

## Issues Encountered

- **`git bisect` across this plan hits no non-building commit.** Both RED commits compile: Task 1's RED registers a target whose only source is the test, which fails at the `#include`, and Task 2's RED ships the header so the failure is a link error. That is a deliberate improvement on 09-01, which recorded two non-building RED commits as a bisect hazard — see the last entry under "Decisions Made".
- **One case is conditional on not running as root** (`A temporary that cannot be created in the target's own directory…`). Directory permissions are not enforced for uid 0, so on a root-only host that case `SKIP`s and the T-9-07/T-9-08 evidence is not collected there. It ran and passed here (uid 1000, 6 assertions); the caveat is filed in `.planning/WINDOWS.md` so it is visible at ship time rather than discovered in a container build.
- **Nothing else.** No sanitizer findings, no new warnings, no flaky case.

## Known Stubs

None. No stub value, no placeholder text, no `TODO`/`FIXME`, no unconditionally skipped test, and no `<verify>` command left unrun.

## Threat Flags

None. The plan's `<threat_model>` covers every trust boundary this plan actually crossed, and the new surface introduced (a value-content check at the GUI→file boundary) is a *mitigation* added under deviation 3 rather than an unmodelled exposure.

## Flagged Assumption (carried from the plan's edge-probe accounting)

CGUI-03's file half remains a **flagged assumption**: this plan assumes the set of keys the GUI may edit is exactly `configFileManagedKeys()`, and that `rule-*` is deliberately outside it. The list is now 21 concrete strings in `src/ConfigFileWriter.cpp`, asserted by count and by exact contents, so a reviewer who disagrees with that boundary has a single place to argue and a test that will fail the moment anyone moves it.

CGUI-02's `concurrency` edge is **resolved (explicit)** for the codec half, authored as a plain truth in the header and asserted by two cases: decoding is a pure function of one line, holds no state between calls, and resets its out-parameter before anything else — so two decoders over different lines cannot affect each other and an interrupted decode leaves no state to corrupt. The socket half of the same item is carried by 09-03.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- **Ready for 09-03** (socket server): `ConfigProtocol.h` is the wire, `kConfigProtocolMaxLine` is the read bound the server should mirror, and the socket path spelling plus `_WM2_CONFIG_SOCKET` are frozen in DISC-01 for `configSocketPath()` and the property publish to implement.
- **Ready for 09-04** (`wm2-ctl`): the header links with no X11 and no GTK, which is what D-17's "no GTK dependency in the WM package" requires. `ConfigDecodeResult`'s four verdicts map onto DISC-01a's exit codes.
- **Ready for 09-06/09-08** (menu page, GUI): `configFileWrite()` takes the GUI's `AppEntry` rows directly and round-trips them through the parser's accumulator; `configFileManagedKeys()` is the exact list the form must cover.
- **Owed to whoever adds a `Config` field next:** the managed-key count `21` is asserted explicitly in `tests/test_config_writer.cpp`, so adding a setting without adding it to `configFileManagedKeys()` fails a test rather than silently shipping a control the GUI cannot save.
- **No blockers.**

## Self-Check: PASSED

- All 5 created files and the 1 modified file exist on disk with the changes described.
- All 5 commit hashes (`a8d88f9`, `87fb868`, `ebb2769`, `63b8b2a`, `0cfde8f`) are present in `git log`.
- Every `<acceptance_criteria>` item from all three build tasks was executed and passes — the table under "Verification Run" is the output, not a restatement.
- The plan-level `<verification>` is green on all three points: both new labels select a non-zero number of cases (25 and 31) and pass; neither binary links `libX11`, `libXft` or `libgtk`; `bash scripts/gates/build-all.sh debug` → `build-all OK: debug`.
- `requirements.ready-ids` reports 0 of 2 ready: CGUI-02 and CGUI-03 are also declared by later plans in this phase, so they correctly stay `Pending` until those finish.

---
*Phase: 09-config-gui-ipc*
*Completed: 2026-09-06*
