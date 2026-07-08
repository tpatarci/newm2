---
phase: 07-root-menu-application-discovery
reviewed: 2026-07-08T00:00:00Z
depth: standard
files_reviewed: 17
files_reviewed_list:
  - include/AppCache.h
  - include/AppEntry.h
  - include/BinaryScanner.h
  - include/Config.h
  - include/DesktopEntry.h
  - include/Manager.h
  - src/AppCache.cpp
  - src/BinaryScanner.cpp
  - src/Buttons.cpp
  - src/Config.cpp
  - src/DesktopEntry.cpp
  - src/main.cpp
  - src/Manager.cpp
  - tests/test_appcache.cpp
  - tests/test_binaryscanner.cpp
  - tests/test_config.cpp
  - tests/test_desktopentry.cpp
findings:
  critical: 2
  warning: 3
  info: 3
  total: 8
critical_open: 0
warning_open: 1
info_open: 3
status: fixed
---

# Phase 07: Code Review Report

**Reviewed:** 2026-07-08
**Depth:** standard
**Files Reviewed:** 17
**Status:** fixed (both Critical findings and 2/3 Warnings resolved same-day; see Resolution section)

## Resolution (2026-07-08)

Both **Critical** findings and two of three **Warning** findings were fixed directly by the orchestrator immediately after this review, then independently re-verified (not just re-compiled):

- **CR-01** (submenu mispositioned): Fixed in `93410c8`. Re-verified interactively on Xvfb — opened the menu away from the screen origin and hovered a middle category row ("Graphics"); the submenu now appears precisely anchored to that row, confirming the original bug (and that the initial manual-verification checkpoint had coincidentally masked it by testing only a corner-click + last-row hover).
- **CR-02** (CRLF blank-line crash): Fixed in `8eaad4d`. Verified by reproducing the pre-fix crash standalone (`-D_GLIBCXX_ASSERTIONS` build, exit 134/SIGABRT on the exact input) and confirming the post-fix build handles the same input cleanly (exit 0). Added a regression test (`tests/test_desktopentry.cpp`, "parseFile handles a CRLF-terminated blank line without crashing").
- **WR-01** (grab leak on empty category) and **WR-02** (unchecked re-grab): Fixed in `93410c8`.

Full project test suite re-run after all fixes: **137/137 passing** (136 prior + 1 new regression test).

**Left open, not urgent** (per this report's own severity assessment — self-inflicted/hand-edit-only or purely theoretical, no active trigger):
- WR-03 (hand-rolled JSON key lookup is lexical, not structural)
- IN-01 (dead fallback branch in `menuLabelFn`)
- IN-02 (unchecked `uint64` addition in ELF PT_LOAD bounds check)
- IN-03 (redundant double-`open()` in `scanUsrBin()`)

## Summary

Phase 7 adds XDG `.desktop` parsing, a heuristic ELF `DT_NEEDED` scanner, a
hand-rolled JSON app cache, and a two-level (category submenu) root menu UI.
The security-sensitive invariants called out in the task brief mostly hold:
`BinaryScanner.cpp`'s ELF parser is carefully bounds-checked against
`fileSize` at every offset/size computation before dereferencing, and
`launchApp()`/`spawnArgv()` in `src/Manager.cpp` correctly route
Desktop/BinaryScan-sourced entries through `execvp()` only — never through a
shell — with the shell path gated strictly to `Manual` + `execUsingShell`
entries (trusted, user-authored config values). Those two invariants are
sound.

However, this review found two BLOCKER-level bugs: (1) the new category
submenu UI in `src/Buttons.cpp` positions the submenu window using
overwritten, event-relative mouse coordinates instead of the outer menu
window's screen-space origin, so the submenu the phase's headline feature
almost never appears next to the row the user hovered; and (2)
`DesktopEntry.cpp`'s `.desktop` line parser calls `std::string::front()` on
a string that can be empty for a CRLF-terminated blank line, which is
undefined behaviour and demonstrably aborts the process (`SIGABRT`) on a
hardened libstdc++ build (`_GLIBCXX_ASSERTIONS`), making it a
malformed-input crash (denial of service) in a code path that parses files
from directories that are not fully trusted (`~/.local/share/applications`,
third-party-packaged `/usr/share/applications` entries).

Additional warnings cover a latent (currently-unreachable but
un-cleaned-up) X11 grab leak in the submenu code, an unchecked re-grab
return, and minor hand-rolled-JSON/dead-code robustness notes.

## Critical Issues

### CR-01: Category submenu is positioned using stale/overwritten mouse-relative coordinates, not the outer menu's screen position

**File:** `src/Buttons.cpp:148-167, 225-262, 313-334, 337-378`
**Issue:**
In `WindowManager::menu()`, `x`/`y` are first computed as the **screen-space
origin** of `m_menuWindow` (lines 148-162) and used to actually place the
window via `XMoveResizeWindow` (line 167). But inside the same function's
event loop, both the `MotionNotify` case (lines 227-228) and the
`ButtonRelease` case (lines 206-207) **reassign these same `x`/`y`
variables** to `event.xbutton.x` / `event.xbutton.y - 11` — which, because
the pointer is actively grabbed on `m_menuWindow` with `owner_events=False`
(see `attemptGrab()`), are coordinates **relative to `m_menuWindow`**, not
the screen.

When a category row is hovered (line 260) or clicked (line 328),
`openCategorySubmenu(..., x, y, maxWidth, ...)` is called with these
now-corrupted `x`/`y` values passed as `outerX`/`outerY`. Inside
`openCategorySubmenu` (`src/Buttons.cpp:368-376`):

```cpp
int subX = outerX + outerMaxWidth;
int subY = outerY + rowIndex * entryHeight;
```

`outerX`/`outerY` are expected (per the function's own doc comment,
`Manager.h:166-168`, "Anchor to the right of the outer menu... mirroring
the outer menu's own edge-avoidance logic") to be the outer menu window's
on-screen origin. Instead they are small, window-relative cursor
coordinates (typically in the range `[0, maxWidth]` / `[0, totalHeight]`).
Any time the root menu is opened away from the screen's top-left corner
(i.e., almost always), the submenu is placed at a location unrelated to the
actual menu, usually clustered near screen coordinates close to `(0,0)`
instead of directly beside the hovered row. This breaks the phase's
headline new feature.

**Fix:** Capture the window's screen-space origin into dedicated variables
before the event loop begins, and pass those (not the reused `x`/`y`) to
`openCategorySubmenu()`:

```cpp
// after the edge-avoidance adjustments, right before XMoveResizeWindow:
const int winX = x;
const int winY = y;
XMoveResizeWindow(display(), m_menuWindow, winX, winY, maxWidth, totalHeight);
...
// MotionNotify case:
openCategorySubmenu(m_appCategories[selecting - nh], e, winX, winY, maxWidth, selecting);
...
// post-loop click fallback:
openCategorySubmenu(m_appCategories[selecting - nh], e, winX, winY, maxWidth, selecting);
```

(`x`/`y` can continue to be reused freely for hit-testing within the loop —
just stop passing them to `openCategorySubmenu` in place of the window
origin.)

### CR-02: `.desktop` line parser calls `front()` on a potentially-empty string — UB, aborts on hardened builds

**File:** `src/DesktopEntry.cpp:249-256`
**Issue:**
```cpp
auto start = line.find_first_not_of(" \t");
if (start == std::string::npos) continue;  // blank line
if (line[start] == '#') continue;           // comment

auto end = line.find_last_not_of(" \t\r\n");
std::string trimmedLine = line.substr(start, end - start + 1);

if (trimmedLine.front() == '[') {
```
`start` is located using a charset that excludes only `" \t"` (not `\r`),
while `end` is located using a charset that excludes `" \t\r\n"`. For a
line consisting solely of a trailing carriage return plus optional
spaces/tabs (e.g. the value of a blank line in a CRLF-terminated
`.desktop` file — `line == "\r"` after `std::getline` strips the `\n`),
`start` finds the `\r` (since `\r` is not in `" \t"`), but `end` returns
`std::string::npos` (since `\r` **is** excluded by `end`'s charset). The
subsequent `end - start + 1` computation then wraps around
(`npos - start + 1` underflows to `0` for `size_t` arithmetic), so
`trimmedLine.substr(start, 0)` yields an **empty string**, and
`trimmedLine.front()` is called on it.

`std::string::front()` on an empty string is undefined behaviour per the
C++ standard. Verified locally: it does not crash with a stock
`-O2`/`-O0` libstdc++ build (returns the SSO null terminator), but **it
aborts the process (`SIGABRT`) when built with `-D_GLIBCXX_ASSERTIONS`**
(a hardening flag several distributions enable by default for hardened
package builds). Since `.desktop` files are parsed from
`XDG_DATA_HOME`/`XDG_DATA_DIRS`-derived directories — some of which
(`~/.local/share/applications`) are fully user-writable and others come
from third-party-packaged content the user did not necessarily audit —
this is a reachable crash/DoS on a malformed or CRLF-terminated
`.desktop` file, not merely a theoretical UB nit. No test in
`tests/test_desktopentry.cpp` exercises a CRLF or all-whitespace-plus-`\r`
line, so this gap is uncaught.

**Fix:** Make the leading trim consistent with the trailing trim (include
`\r`/`\n` in both), and/or add an explicit empty check before `front()`:

```cpp
auto start = line.find_first_not_of(" \t\r\n");
if (start == std::string::npos) continue;  // blank line
if (line[start] == '#') continue;           // comment

auto end = line.find_last_not_of(" \t\r\n");
std::string trimmedLine = line.substr(start, end - start + 1);
if (trimmedLine.empty()) continue;  // defense in depth

if (trimmedLine.front() == '[') {
```

## Warnings

### WR-01: `openCategorySubmenu()`'s `n2 == 0` early-return leaks the outer menu's X11 pointer grab

**File:** `src/Buttons.cpp:341-342` (guard), call sites `src/Buttons.cpp:260-261, 328`
**Issue:** `openCategorySubmenu()` begins with:
```cpp
int n2 = static_cast<int>(category.second.size());
if (n2 == 0) return;
```
This returns **before** `XUngrabPointer()` is called (that only happens
later, at line 393, right before the submenu's own grab attempt) and
before `m_menuWindow` is unmapped. The caller, `menu()`'s `MotionNotify`
case, unconditionally `return`s immediately after calling
`openCategorySubmenu()` (line 261) — it never reaches the
`releaseGrab()`/`XUnmapWindow()` cleanup that the normal `ButtonRelease`
path performs. If this branch is ever reached, the X11 pointer grab
established by `menu()`'s `attemptGrab(m_menuWindow, ...)` is **never
released**, freezing all pointer input to the entire X session (nothing
else calls `XUngrabPointer` for that grab) until the WM process exits.

Currently `buildAppCategories()` (`src/Manager.cpp:194-214`) can only ever
populate a bucket that has at least one entry (buckets are created via the
same `push_back` that adds the entry), so `n2 == 0` is unreachable *today*.
But the guard's presence signals the author anticipated this case might
occur, and as written it is a landmine: any future change that can produce
an empty category (e.g., a filtering step added later) turns this into a
full-desktop hang.

**Fix:** Perform the same cleanup as the "grab failed" path before
returning:
```cpp
if (n2 == 0) {
    // Nothing to show; caller already holds the outer grab, so nothing to
    // release here — but the caller's return must not skip its own
    // grab-release path. Simplest fix: don't special-case at all, or have
    // menu() call releaseGrab()/XUnmapWindow() when openCategorySubmenu()
    // signals "nothing happened" (e.g. via a bool return value).
}
```
Concretely, changing `openCategorySubmenu` to return a `bool` ("did I take
over the interaction?") and having `menu()` fall through to its own
`releaseGrab`/unmap path when it returns `false` closes this hole cleanly.

### WR-02: Unchecked return value when re-establishing the outer menu's grab after a failed submenu grab

**File:** `src/Buttons.cpp:395-399`
**Issue:**
```cpp
if (attemptGrab(m_submenuWindow, None, MenuGrabMask, e->time) != GrabSuccess) {
    XUnmapWindow(display(), m_submenuWindow);
    attemptGrab(m_menuWindow, None, MenuGrabMask, e->time);
    return;
}
```
If the fallback `attemptGrab(m_menuWindow, ...)` also fails (e.g. another
client grabbed the pointer in between), the function still just `return`s.
`menu()`'s outer `while (!done)` loop then keeps calling
`XMaskEvent(display(), MenuMask, &event)`, which will only ever receive
events for `m_menuWindow` if the user happens to interact with it directly
(no active grab), leaving the interaction in a degraded, hard-to－escape
state rather than cleanly aborting the menu.

**Fix:** Check the second `attemptGrab()`'s return value too; if it also
fails, unmap `m_menuWindow` and abort the whole menu interaction (mirroring
how `menu()` itself handles its initial grab failure at
`src/Buttons.cpp:180-183`).

### WR-03: Hand-rolled JSON `extractString`/`extractStringArray` key search is not structurally scoped, only lexically

**File:** `src/AppCache.cpp:148-172, 177-216`
**Issue:** `extractString(obj, "name")` finds the **first textual
occurrence** of the literal substring `"name"` anywhere in `obj`, rather
than parsing `obj`'s actual key/value structure. Since `obj` is scoped to
a single entry's `{...}` object, the realistic collision window is narrow
(it would require a quoted token equal to one of the recognized keys —
`name`, `exec`, `icon`, `category`, `source` — to appear literally inside
another field's string value, e.g. inside an `exec` array element such as
`"--name"`, which does *not* match the pattern `"name"` exactly, but a
contrived value like `"\"name\""` embedded in `exec` would). This is a
purpose-built minimal parser (documented as such), so this is not a
correctness regression against its stated scope, but given the review
brief explicitly calls out "the hand-rolled JSON reader ... handles a
cache file that could be corrupted or hand-edited," it's worth flagging:
a hand-edited cache crafted to place a quoted `"name"`-shaped token inside
another field can cause `extractString` to pick up the wrong value
silently (no error, no warning) rather than failing closed.

**Fix:** Not urgent given the narrow, self-inflicted (hand-edit-only)
trigger, but consider anchoring the search to require the pattern be
preceded by `{` or `,` (mod whitespace) to reduce false-positive key
matches, or accept the risk given the file is fully self-owned
(written only by `AppCache::write()` under normal operation).

## Info

### IN-01: Dead/unreachable fallback branch in `menuLabelFn`

**File:** `src/Buttons.cpp:125-131`
**Issue:**
```cpp
auto menuLabelFn = [&](int idx) -> const char* {
    if (idx == 0) return m_menuCreateLabel;
    if (idx < nh) return clients[idx - 1]->label().c_str();
    if (idx < nh + numCategories) return m_appCategories[idx - nh].first.c_str();
    if (allowExit && idx == n - 1) return "[Exit wm2]";
    return clients[idx - 1]->label().c_str();   // <-- unreachable
};
```
Given `n = nh + numCategories (+ 1 if allowExit)`, every `idx` in
`[0, n)` is covered by one of the first four branches (when `allowExit`
is false, `nh + numCategories == n`, so `idx < nh + numCategories` already
covers the last index; when `allowExit` is true, the only remaining index
is `n - 1`, caught by the fourth branch). The final `return` is therefore
dead code, and if it were ever reached (e.g. after a future edit
loosens one of the earlier bounds), it would call
`clients[idx - 1]` with an out-of-range `idx`, an out-of-bounds
`std::vector::operator[]` access.

**Fix:** Replace the trailing fallback with an explicit assertion or a
safe default (e.g. `return "?";`) to fail loudly/safely instead of
silently modeling an "impossible" state as a client-array access.

### IN-02: `BinaryScanner::readNeededLibraries()` PT_LOAD vaddr/memsz bound has no overflow guard

**File:** `src/BinaryScanner.cpp:147-153`
**Issue:**
```cpp
for (const LoadSegment& seg : loadSegments) {
    if (strtabVaddr >= seg.vaddr && strtabVaddr < seg.vaddr + seg.memsz) {
```
`seg.vaddr + seg.memsz` is unchecked `uint64_t` addition; a maliciously
crafted `PT_LOAD` header with `p_vaddr`/`p_memsz` chosen to overflow could
wrap the sum to a small value and make the containment test behave
unexpectedly. Because every subsequent byte access is still separately
bounds-checked against `fileSize` before dereferencing (`nameOffset >=
fileSize`, `len == maxLen`, etc.), this cannot cause an out-of-bounds
read — worst case is a misclassification (GUI vs CLI) of a
deliberately-crafted binary. Given `/usr/bin` normally requires root to
write to, the practical exploitability is very low, but it's worth a
defensive bound check for completeness given this module's stated design
goal of being resilient to untrusted binary input.

**Fix:** Guard the addition, e.g.
`if (seg.memsz > UINT64_MAX - seg.vaddr) continue;` before the comparison.

### IN-03: `scanUsrBin()` opens each candidate file twice (shebang check, then `readNeededLibraries()` reopens by path)

**File:** `src/BinaryScanner.cpp:222-247`
**Issue:** The shebang pre-check opens the file via `fd`, reads 2 bytes,
and `close()`s it; if it's not a shebang script, `readNeededLibraries(path)`
is called, which `open()`s the *same path* again. Between the two
`open()` calls, the underlying file could theoretically be replaced (e.g.
concurrent package upgrade). This is a narrow, low-severity TOCTOU window
(the code elsewhere is careful to avoid exactly this pattern per the
`T-7-04` comments), and the worst outcome is scanning a different file's
contents than intended for classification purposes — not a memory-safety
issue. Noting for consistency with the module's own stated TOCTOU-avoidance
goal.

**Fix:** Pass the already-open `fd` into a variant of the ELF-parsing
logic (`fstat`+`mmap` directly on that `fd`) instead of reopening by path,
matching the pattern already used for the executable-bit check earlier in
the same loop.

---

_Reviewed: 2026-07-08_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
