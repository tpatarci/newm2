---
phase: 09-config-gui-ipc
plan: 03
subsystem: infra
tags: [unix-socket, ipc, poll, so-peercred, x11-property, catch2, posix, event-loop]

requires:
  - phase: 09-config-gui-ipc
    provides: "09-02's `include/ConfigProtocol.h` -- the frozen version-1 codec (`kConfigProtocolVersion`, `kConfigProtocolMaxLine`, `ConfigMessageType`, `configProtocolEncode/Decode`) and the DISC-01 block that froze the socket path spelling and the `_WM2_CONFIG_SOCKET` property name built here"
  - phase: 08.5-v1.0-closeout
    provides: "WINDOWS.md ledger 8's interruptible `modalWait()` -- the second poll site this plan had to reach; `include/EventPump.h` and `include/TimestampWait.h` as the poll()-driven house pattern; `tests/support/WmFixture.h`; the 08.5-06 guard-ordering rule and 08.5-13's QuietXErrors"
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    provides: "D-12's internal-test-lever precedent (`WM2_FORCE_NO_SHAPE`) -- read once, no CLI flag, kept out of user documentation"
provides:
  - "`include/SocketServer.h` / `src/SocketServer.cpp`: the display-free socket decision plus its POSIX implementation"
  - "`enum class SocketAction { Accept, ReadClient, CloseClient, Idle }`, `enum class SocketRole`, `socketServerDecide()`, `socketFailedRevents()`"
  - "`configSocketDirectory()`, `configSocketPath()`, `configSocketPathFits()` -- DISC-02 resolved"
  - "`enum class PeerVerdict`, `configSocketPeerVerdict()`, `configSocketPeerUid()`; `enum class StaleVerdict`, `configSocketStaleVerdict()`"
  - "`class ConfigSocketServer` (listen / appendPollFds / service / broadcast / clientCount / timeoutHintMs / close), with `ConfigSocketRequest` and `ConfigSocketReply` as the transport/policy seam"
  - "`WindowManager::buildPollSet()` and the named indices `kPollFdX`, `kPollFdPipe`, `kPollFdFixedCount` -- ONE descriptor set consumed by both `nextEvent()` and `modalWait()`"
  - "`Atoms::wm2_configSocket` and the `_WM2_CONFIG_SOCKET` root-window property (XA_STRING, format 8) holding the socket path"
  - "`WindowManager::handleConfigRequest()` (D-15's handshake rule) and `WindowManager::statusReplyMessage()` (D-14's seven-field ceiling, one site)"
  - "`WM2_VERSION` compile definition, taken from `${PROJECT_VERSION}`"
  - "`WM2_SOCKET_FORCE_FOREIGN` -- a strictly-narrowing internal test lever for the refusal path"
  - "ctest labels `config_socket` (14 cases, display-free) and `wm_socket` (17 cases, against the real binary)"
  - "The X11 interop guard in `include/ConfigProtocol.h`: Xlib's `#define Status int` replaced by a typedef so the frozen `Status` enumerator is nameable"
affects: [09-04-wm2-ctl, 09-05-live-apply, 09-06-menu-page, 09-07-protocol-client, 09-08-gui, 09-09-release]

actuals:
  tokens: 36000     # chars/4 over the realized diff (142,154 chars, afc1d67..cc86162)
  tasks: 3
  commits: 3

tech-stack:
  added:
    - "AF_UNIX SOCK_STREAM listening socket in the window manager process (POSIX, no new library)"
    - "SO_PEERCRED peer-credential authentication"
  patterns:
    - "One shared, growable `std::vector<struct pollfd>` built by a single member function and consumed by every poll site, with named indices instead of literals"
    - "A display-free header carrying a pure decision function plus a POSIX .cpp, compiled straight into its own Catch2 target (the include/MenuPaint.h + test_config pattern, extended to a class with state)"
    - "Transport/policy seam: ConfigSocketRequest/ConfigSocketReply, so descriptors and buffers live in SocketServer and message MEANING lives in WindowManager"
    - "Strictly-narrowing internal test levers -- a lever that can only refuse, never admit, so it cannot widen a security boundary by construction"
    - "Mutation-checked negative assertions: a guard is recorded as proven only after the defect it forbids has been introduced and seen to redden it"

key-files:
  created:
    - include/SocketServer.h
    - src/SocketServer.cpp
    - tests/test_config_socket.cpp
    - tests/test_wm_socket.cpp
  modified:
    - src/Events.cpp
    - include/Manager.h
    - src/Manager.cpp
    - include/ConfigProtocol.h
    - CMakeLists.txt
    - docs/RELEASE-NOTES.md

key-decisions:
  - "DISC-02 -- socket path is $XDG_RUNTIME_DIR/wm2-born-again/socket<display>, fallback /tmp/wm2-born-again-<uid>/socket<display>, display sanitised by replacing every character outside [A-Za-z0-9._-] with '_'"
  - "DISC-03 -- the path is published on the root window as _WM2_CONFIG_SOCKET (XA_STRING, format 8), written immediately after _NET_SUPPORTING_WM_CHECK; its ABSENCE is the honest answer to 'is there a socket?'"
  - "DISC-04 -- SIGHUP left exactly as it is (shared exit handler); reload is a socket message and nothing else"
  - "DISC-06 -- servicing the socket is a fourth, SILENT case in modalWait(): never Event, never Interrupted, so no existing caller's contract changes"
  - "socketServerDecide() takes a defaulted SocketRole, because a readable listener and a readable connection are different actions over identical revents"
  - "'managed' counts every window under management, hidden ones INCLUDED -- addToHiddenList() moves rather than copies, so a bare m_clients.size() would report a window manager losing windows as the user hides them"
  - "Get/Set/Reload are refused by name WITHOUT closing the connection: they are frozen in the contract and served by a later plan in this phase"
  - "A path holding something that is not a socket reads Live, never Stale: this process never unlinks a file it did not create"
  - "Xlib's `#define Status int` is replaced by a typedef rather than removed, because X11 extension headers declare functions RETURNING Status"

patterns-established:
  - "The shared poll set: any future descriptor the window manager must multiplex is added in buildPollSet() ONCE and is thereby serviced at both poll sites"
  - "Every refusal case ends by asserting the window manager is still alive and still framing a newly mapped client -- a boundary that protects by crashing is not a mitigation"
  - "A negative security assertion is written with a deliberately distinctive needle, so it can actually fail"

requirements-completed: [CGUI-02]

coverage:
  - id: D1
    description: "The window manager listens on a Unix domain socket and answers hello and status while idle"
    requirement: CGUI-02
    verification:
      - kind: integration
        ref: "tests/test_wm_socket.cpp#An idle window manager answers hello and status"
        status: pass
      - kind: integration
        ref: "tests/test_wm_socket.cpp#Asking for status twice returns the same field set"
        status: pass
    human_judgment: false
  - id: D2
    description: "The same round trip succeeds while a modal pointer grab is held, because both poll sites consume one shared descriptor set"
    requirement: CGUI-02
    verification:
      - kind: integration
        ref: "tests/test_wm_socket.cpp#The socket answers while a modal pointer grab is held"
        status: pass
      - kind: other
        ref: "grep -c 'struct pollfd fds\\[2\\]' src/Events.cpp == 0"
        status: pass
      - kind: other
        ref: "reddening check: reverting only the modalWait() servicing hunk turns the held-grab case red (15.48 s timeout), everything else stays green"
        status: pass
    human_judgment: false
  - id: D3
    description: "The socket path is published on the root window as _WM2_CONFIG_SOCKET so a client discovers it"
    requirement: CGUI-02
    verification:
      - kind: integration
        ref: "tests/test_wm_socket.cpp#The socket path is published on the root window and is a socket"
        status: pass
      - kind: manual_procedural
        ref: "xprop -root _WM2_CONFIG_SOCKET on a private Xvfb :171 printed /run/user/1000/wm2-born-again/socket_171"
        status: pass
    human_judgment: false
  - id: D4
    description: "Only the window manager's own uid may connect; any other uid is closed before a protocol byte and logged once per uid"
    requirement: CGUI-02
    verification:
      - kind: unit
        ref: "tests/test_config_socket.cpp#A peer with this uid is admitted and any other uid is not"
        status: pass
      - kind: unit
        ref: "tests/test_config_socket.cpp#A descriptor the kernel will not vouch for is not admitted"
        status: pass
      - kind: integration
        ref: "tests/test_wm_socket.cpp#A refused peer is closed before a protocol byte and logged once per uid"
        status: pass
    human_judgment: false
  - id: D5
    description: "The socket lives in a mode-0700 directory owned by this user and is itself mode 0600"
    requirement: CGUI-02
    verification:
      - kind: integration
        ref: "tests/test_wm_socket.cpp#The socket directory is 0700 and the socket is 0600"
        status: pass
    human_judgment: false
  - id: D6
    description: "A socket left by a crashed window manager is reclaimed at startup; one a live window manager holds is not"
    requirement: CGUI-02
    verification:
      - kind: unit
        ref: "tests/test_config_socket.cpp#An abandoned socket is stale and a live one is not"
        status: pass
      - kind: unit
        ref: "tests/test_config_socket.cpp#A file that is not a socket is never reclaimed"
        status: pass
      - kind: integration
        ref: "tests/test_wm_socket.cpp#A socket left by a crashed predecessor is reclaimed"
        status: pass
    human_judgment: false
  - id: D7
    description: "The status reply carries exactly seven fields and no per-window title, class, instance or geometry"
    requirement: CGUI-02
    verification:
      - kind: integration
        ref: "tests/test_wm_socket.cpp#The status counts are real and no window's identity is in the reply"
        status: pass
      - kind: unit
        ref: "tests/test_wm_socket.cpp#The status assembly does not reach any per-window identity"
        status: pass
      - kind: other
        ref: "awk '/statusReply/,/^}/' src/Manager.cpp | grep -cE 'label\\(\\)|className|instanceName' == 0"
        status: pass
      - kind: other
        ref: "mutation check: adding title fields built from Client::label() reddens all three guards, green again on revert"
        status: pass
    human_judgment: false
  - id: D8
    description: "Two clients connected at once each get their own replies; one disconnecting mid-message disturbs neither the other nor the window manager"
    requirement: CGUI-02
    verification:
      - kind: integration
        ref: "tests/test_wm_socket.cpp#Two clients connected at once each get their own replies"
        status: pass
      - kind: integration
        ref: "tests/test_wm_socket.cpp#A client vanishing mid-request costs only its own connection"
        status: pass
    human_judgment: false
  - id: D9
    description: "Oversized and newline-free input is refused at the transport with an error reply and a close, and a silent connection is dropped at a bounded deadline"
    requirement: CGUI-02
    verification:
      - kind: integration
        ref: "tests/test_wm_socket.cpp#A line over the protocol bound is refused and the connection closed"
        status: pass
      - kind: integration
        ref: "tests/test_wm_socket.cpp#A client that sends bytes and never a newline is dropped at the bound"
        status: pass
      - kind: other
        ref: "bash scripts/gates/build-all.sh asan -- green, no sanitizer findings"
        status: pass
    human_judgment: false
  - id: D10
    description: "A socket path too long for sockaddr_un is named, not truncated, and the window manager still manages windows"
    requirement: CGUI-02
    verification:
      - kind: unit
        ref: "tests/test_config_socket.cpp#A path too long for the address structure is refused, not truncated"
        status: pass
      - kind: integration
        ref: "tests/test_wm_socket.cpp#A socket path too long for the address structure is named, not truncated"
        status: pass
    human_judgment: false
  - id: D11
    description: "modalWait()'s three existing outcomes are unchanged, so every modal grab consumer behaves exactly as before"
    requirement: CGUI-02
    verification:
      - kind: integration
        ref: "tests/test_wm_socket.cpp#modalWait still returns an event when a matching event arrives"
        status: pass
      - kind: integration
        ref: "tests/test_wm_socket.cpp#modalWait still reports an interruption while a grab is held"
        status: pass
      - kind: integration
        ref: "tests/test_wm_socket.cpp#modalWait still times out, so a held tab button still reaches the delay"
        status: pass
      - kind: other
        ref: "ctest -L '^(eventloop|wm_focus|wm_geometry)$' -- 50/50 green"
        status: pass
    human_judgment: false

duration: 68 min
completed: 2026-09-06
status: complete
---

# Phase 09 Plan 03: The Configuration Socket Server Summary

**The window manager now listens on a uid-authenticated Unix domain socket, publishes its path on the root window, and answers hello and status equally while idle and while a modal pointer grab is held — because `nextEvent()` and `modalWait()` were converted, in one commit, from two hardcoded `pollfd` arrays into one shared descriptor set.**

## Performance

- **Duration:** 68 min
- **Started:** 2026-09-06T07:23Z
- **Completed:** 2026-09-06T08:31Z
- **Tasks:** 3 of 3
- **Files modified:** 10 (4 created, 6 modified)
- **Tests added:** 31 (14 `[config_socket]`, 17 `[wm_socket]`); full debug suite 406 → 437, all green

## Accomplishments

- **The phase's named central risk is closed, and proven closed.** `src/Events.cpp` declared `struct pollfd fds[2]` twice — once in `nextEvent()`, once in `modalWait()` — and every modal grab in the codebase (root menu, move, resize, tab-button hold, gesture recogniser) funnels through the second. Both are now one `WindowManager::buildPollSet()` with named indices `kPollFdX` / `kPollFdPipe` / `kPollFdFixedCount`. The held-grab round trip is the case that goes red if only one site is reached, and the reddening check below confirms it does.
- **A working socket server**: `ConfigSocketServer` accepts, frames, dispatches and replies without ever blocking the window manager's only thread — every descriptor non-blocking and close-on-exec, one `recv()` per readable notification, replies buffered and flushed on `POLLOUT`, and `poll()` timeouts clamped so a time-based silence deadline can actually fire.
- **D-16's boundary enforced in both halves and asserted in both**: a mode-0700 directory whose ownership and mode are *verified* rather than trusted, a mode-0600 socket, and an `SO_PEERCRED` uid comparison performed before the first read — with one warning per foreign uid rather than one per attempt.
- **D-14's ceiling enforced at three levels**: the assembly emits exactly seven fields; a behavioural case maps three clients with deliberately distinctive titles and a distinctive class and asserts none of them appears anywhere in the reply text; and a source-level case reads the assembly region out of `src/Manager.cpp` and asserts it reaches no per-window identity accessor. All three were mutation-checked.
- **DISC-02, DISC-03, DISC-04 and DISC-06 decided and recorded in code**, not merely in the plan.

## Task Commits

1. **Task 1 (tracer): one descriptor set, both poll sites, and a client that gets an answer through a held grab** — `c7773c1` (feat)
2. **Task 2: the boundary — same uid only, stale sockets reclaimed, oversized messages refused** — `1543424` (test)
3. **Task 3: what the socket may say, and what happens when two clients say it at once** — `cc86162` (test)

## Files Created/Modified

- `include/SocketServer.h` (new, 310 lines) — the pure `socketServerDecide()` and its four named actions, the DISC-02 path helpers, the `PeerVerdict` / `StaleVerdict` boundary verdicts, and `ConfigSocketServer`. Includes no X11, Xft, GTK or GLib header.
- `src/SocketServer.cpp` (new, 650 lines) — directory creation and verification, stale reclamation, bind/chmod/listen, accept with the uid check before any read, bounded per-connection input and output buffers, the silence deadline, and warn-once-per-uid.
- `src/Events.cpp` — `buildPollSet()`, `clampPollTimeoutForSocket()`, `serviceConfigSocket()`; both poll sites converted in the same commit.
- `include/Manager.h` — the named descriptor indices, `m_socketServer`, `m_startTime`, the new private members, and `Atoms::wm2_configSocket`.
- `src/Manager.cpp` — atom interned, socket started before `initialiseScreen()`, `_WM2_CONFIG_SOCKET` published immediately after `_NET_SUPPORTING_WM_CHECK`, `statusReplyMessage()`, `handleConfigRequest()`, socket closed and unlinked first in `release()`.
- `include/ConfigProtocol.h` — X11 interop guard only; no contract change.
- `tests/test_config_socket.cpp` (new) — 14 display-free cases including an exhaustive totality check over all 256 low-byte revents values for both roles.
- `tests/test_wm_socket.cpp` (new) — 17 cases against the real binary.
- `CMakeLists.txt` — `src/SocketServer.cpp` in the WM target, `WM2_VERSION` from `${PROJECT_VERSION}`, and the two new test targets.
- `docs/RELEASE-NOTES.md` — a "The configuration socket" subsection under "For developers".

## Decisions Made

Beyond the DISC items recorded in frontmatter:

- **`socketServerDecide()` takes a defaulted `SocketRole`.** The plan specified `socketServerDecide(short revents)` with a `SocketAction::Accept` enumerator, but a readable listener and a readable connection are different actions over identical revents, so the role has to be part of the question or `Accept` is unreachable. The parameter is defaulted to `Client`, so the plan's exact spelling `socketServerDecide(revents)` compiles and means the common case.
- **Failure bits are checked *before* readability for the listener and *after* it for a connection.** A peer that wrote a final request and closed reports `POLLIN | POLLHUP` together; reading first is what makes its last message answerable. A listener has no payload to rescue. Both orders are asserted directly.
- **`Get` / `Set` / `Reload` are refused by name and the connection is kept open.** They are frozen in the 09-02 contract but served by later plans in this phase; a client that asks early learns this build does not serve them yet and can carry on with `status`.
- **The socket is closed and unlinked first in `release()`**, before any X resource, because it owns plain descriptors and a filesystem node that depend on nothing else — and because no client should be able to observe a half-torn window manager.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking] Xlib's `Status` macro made the frozen `Status` enumerator unnameable**

- **Found during:** Task 1, at the first build of `src/Manager.cpp`.
- **Issue:** `X11/Xlib.h:83` contains `#define Status int` — a preprocessor macro, not a typedef. It rewrites the token `Status` everywhere after Xlib is included, including inside `enum class ConfigMessageType { ... Status ... }` and at every `ConfigMessageType::Status` use site. Any translation unit holding both Xlib and `include/ConfigProtocol.h` failed to compile with a cascade of ~30 errors. `status` is one of the eleven message types D-14 froze and D-8.5-01 makes permanent, so renaming the enumerator was not available.
- **Fix:** A guard block in `include/ConfigProtocol.h` that undefines the macro and immediately re-declares `Status` as a `typedef int`. The typedef half is load-bearing: a bare `#undef` broke `X11/extensions/shape.h` and `X11/extensions/Xrandr.h`, both of which declare functions *returning* `Status` (verified — that was the first attempt and it produced 11 new errors). A global typedef does not reach into a scoped enum's own scope, so the two coexist. `Bool`, `True` and `False` are deliberately left alone; the window manager uses all three constantly.
- **Verification:** Full debug tree builds clean; `tests/test_wm_fallbacks.cpp:163` (the one place in the repo that uses Xlib's `Status` type) still compiles and its label is green.
- **Committed in:** `c7773c1`.

**2. [Rule 2 — Missing critical] `poll()` timeouts had to be clamped, or the silence deadline could never fire**

- **Found during:** Task 1, while implementing the hello deadline.
- **Issue:** A connection that never speaks generates no poll event, so the deadline that drops it fires on the passage of time alone. Both poll sites can block indefinitely (`nextEvent()` with no active focus timer, `modalWait()` with `timeoutMs < 0`), so the deadline would never be reached — leaving the descriptor held for the whole session and defeating the plan's own truth about a client that never sends a hello.
- **Fix:** `ConfigSocketServer::timeoutHintMs()` and `WindowManager::clampPollTimeoutForSocket()`; both poll sites clamp.
- **Verification:** `tests/test_wm_socket.cpp#A client that sends bytes and never a newline is dropped at the bound` is green; the three pre-existing `modalWait()` labels stay green, so the clamp did not change any existing wait's observable behaviour.
- **Committed in:** `c7773c1`.

**3. [Rule 3 — Blocking] `[wm_socket]` cases were killed by raced X protocol errors**

- **Found during:** Task 1, first run of the new label.
- **Issue:** 2 of 7 cases died outright with `BadDrawable` on `X_GetGeometry` and no assertion output at all. A child listed by `XQueryTree` can be destroyed before `XGetGeometry` names it, and Xlib's default handler kills the process.
- **Fix:** The `QuietXErrors` static guard from `tests/test_wm_runtime.cpp`, which counts errors rather than merely swallowing them. The helpers already handled the failure correctly; this only lets them reach that code.
- **Verification:** 18/18 green across repeated runs, in both the debug and ASan trees.
- **Committed in:** `c7773c1`.

**4. [Rule 1 — Bug] A test expectation was wrong about display-name sanitisation**

- **Found during:** Task 1, first run of `[config_socket]`.
- **Issue:** The case for `configSocketPath("../../etc/passwd")` expected `socket_________etc_passwd`, forgetting that `.` is a *safe* character and only `/` is replaced.
- **Fix:** Corrected to `socket.._.._etc_passwd`, with a comment recording why the distinction matters (`/` is replaced, so the result is always a leaf name inside the directory and escape is impossible).
- **Verification:** 14/14 `[config_socket]` green.
- **Committed in:** `c7773c1`.

### Planned-scope adjustments

**5. [Naming] `Atoms::wm2_configSocket`, not `Atoms::wm2ConfigSocket`**

The plan's action text names `Atoms::wm2ConfigSocket`. The existing static table is uniformly `prefix_camelCase` (`wm2_running`, `net_supportingWmCheck`), and `CLAUDE.md`'s conventions section is explicit about it. The house convention was followed and the plan's spelling was not.

**6. [Task boundary] The tracer built Task 2's implementation, so Task 2's commit is tests only**

`type="tracer"` requires production quality, not a throwaway, and the access control, stale reclamation and framing bound are not separable from a socket server that can be honestly said to work — a tracer that accepted connections without the uid check would have shipped an open socket between two commits. Task 1 therefore landed all of `src/SocketServer.cpp` and all of `tests/test_config_socket.cpp`. Task 2's commit is the six `[wm_socket]` boundary cases that were genuinely still missing, and Task 3's is the five `[wm_socket]` ceiling and concurrency cases plus the source-level guard. Every acceptance criterion of all three tasks is met; only the commit that carries each line of implementation differs from the plan's file lists.

**7. [Rule 2 — Missing critical] `docs/RELEASE-NOTES.md` was updated**

Not in the plan's `files_modified`, but the project standing rule requires every new user-visible name to be documented, and `_WM2_CONFIG_SOCKET` and the socket path are permanent under D-8.5-01. A "The configuration socket" subsection was added under "For developers", covering discovery, the path, the uid boundary, the framing bound and the status field list.

**8. [Process] Implementation preceded tests within Task 1**

Task 1 carries `tdd="true"`. The header, implementation and both poll sites were written before the test files, so the classic RED-first order was not followed for the display-free cases. It *was* honoured for the task's central claim, and by the plan's own mandated instrument: reverting only the `modalWait()` servicing hunk turned the held-grab case red and left every other case green (see below). Task 3's guards were likewise mutation-checked rather than merely observed green. Recorded as a real process deviation, not excused.

---

**Total deviations:** 4 auto-fixed (1 × Rule 1, 2 × Rule 2, 2 × Rule 3 — item 1 and item 3 both Rule 3) plus 4 recorded scope/process adjustments.
**Impact on plan:** No scope creep. Every auto-fix was required for the plan's own stated behaviour to be true; the two largest (the `Status` macro and the timeout clamp) are things the plan could not have known without building. All three tasks' acceptance criteria are met.

## Evidence

**The reddening check (Task 1 acceptance criterion, mandatory).** With `serviceConfigSocket(fds)` removed from `modalWait()` and left in place in `nextEvent()` — the `modalWait()` hunk alone reverted — the `[wm_socket]` label went:

```
5/7 Test #141: The socket answers while a modal pointer grab is held ...***Failed   15.48 sec
86% tests passed, 1 tests failed out of 7
```

Exactly one case red, and red by *timeout* at the 15 s socket deadline — the signature the plan names for a change that reached only one of the two poll sites. Everything else, including the idle round trip and all three `modalWait()` contract cases, stayed green: no other test in the suite can detect this defect. Restored, and 7/7 green again.

**The D-14 mutation check (Task 3).** Adding `for (const auto& c : m_clients) reply.fields.emplace_back("title", c->label());` to the status assembly turned all three guards red at once — the `awk` region gate 0 → 1, the behavioural negative assertion over the reply text, and the source-level case. Green again on revert. The distinctive titles (`wm2-socket-secret-alpha` and friends) are what make the behavioural assertion capable of failing; a title of `xterm` would have passed by accident.

**Literal `xprop` check (Task 1 acceptance criterion).** Against a private `Xvfb :171 -nolisten tcp`, stopped by the PID started:

```
_WM2_CONFIG_SOCKET(STRING) = "/run/user/1000/wm2-born-again/socket_171"
srw------- /run/user/1000/wm2-born-again/socket_171
drwx------ /run/user/1000/wm2-born-again
```

The socket node was gone after the window manager exited, confirming the unlink in `release()`.

**Display-free linkage (Task 1 acceptance criterion).** `ldd build/debug/test_config_socket` names neither `libX11` nor `libgtk`; `grep -cE '#include *[<"](X11|gtk|glib|gdk)'` over `include/SocketServer.h` and `src/SocketServer.cpp` is 0 for both.

**Gates.**

| Gate | Result |
|---|---|
| `ctest -L '^config_socket$'` | 14/14 |
| `ctest -L '^wm_socket$'` | 17/17 |
| `ctest -L '^(eventloop\|wm_focus\|wm_geometry)$'` | 50/50 |
| `grep -c 'struct pollfd fds\[2\]' src/Events.cpp` | 0 |
| `grep -c 'buildPollSet();' src/Events.cpp` | 2 (called exactly twice) |
| `awk '/statusReply/,/^}/' src/Manager.cpp \| grep -cE 'label\(\)\|className\|instanceName'` | 0 |
| `bash scripts/gates/build-all.sh debug` | 437/437, OK |
| `bash scripts/gates/build-all.sh asan` | OK, **no sanitizer findings** |

## Issues Encountered

- **Leftover socket nodes from ctest fixtures.** Fixtures whose window manager is killed rather than terminated (the SIGKILL case, and the two cases that died before `QuietXErrors` was installed) leave a socket node behind in `$XDG_RUNTIME_DIR/wm2-born-again/`. This is exactly the stale-reclamation scenario and is handled: the next window manager on that display number connects, gets `ECONNREFUSED`, unlinks and binds. Confirmed both by `[config_socket]`'s stale cases and by the `[wm_socket]` crashed-predecessor case. Not a defect; recorded because a reader looking at that directory will see the nodes.
- **No pre-existing `WM2_VERSION`.** The project had no version constant reachable from C++ — only `project(... VERSION 0.1.0)` in CMake. Added as a `target_compile_definitions` on the WM target with a `"0.0.0-unknown"` fallback in `src/Manager.cpp`, so the reported version and the packaged version cannot drift.

## Known Stubs

None. `Get`, `Set` and `Reload` are not stubs: they are frozen contract messages this build deliberately refuses by name, without closing the connection, with the refusal asserted by the protocol's own decode path and the reason recorded in the dispatcher. They are served by 09-04 (`wm2-ctl`) and 09-05 (live apply).

## Threat Flags

None. Every trust boundary this plan opens was already in the plan's `<threat_model>`, and T-9-11 through T-9-19 are each mitigated and asserted. The one caveat worth carrying forward is recorded in code rather than only here: `SO_PEERCRED` captures credentials at `connect()` time, so a process that later changes privileges does not un-authorize a connection it already holds. This does not weaken D-16 — same-uid-at-connect-time is what D-16 asks for — and the comment sits at `configSocketPeerVerdict()`'s declaration so a later audit finds it rather than discovers it.

## User Setup Required

None — no external service configuration required. The socket is created automatically at startup and needs no user action.

## Next Phase Readiness

Ready for **09-04 (`wm2-ctl`)** and **09-07 (protocol client)**. Both need only:

- `_WM2_CONFIG_SOCKET` on the root window for discovery, or `configSocketPath(DisplayString(d))` as the fallback spelling.
- A `hello` carrying `kConfigProtocolVersion` as the first message; anything else closes the connection.
- `include/SocketServer.h` for the path helpers, which are display-free and link with no X11.

Ready for **09-05 (live apply)** with one note: `ConfigSocketServer::broadcast()` exists, sends only to connections that have completed a handshake, and is currently unused — it is the hook a settings-changed notification should use. `handleConfigRequest()` is the single place `Get` / `Set` / `Reload` need to be implemented; they currently return `error` with reason `not served by this build` without closing the connection, so a client written against this build will keep working when they land.

No blockers.

---
*Phase: 09-config-gui-ipc*
*Completed: 2026-09-06*

## Self-Check: PASSED

All five created artifacts exist on disk; all four commits (`c7773c1`, `1543424`, `cc86162`, `66db350`) are present in the repository history.
