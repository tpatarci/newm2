---
phase: 09-config-gui-ipc
reviewed: 2026-09-06T18:41:01Z
depth: standard
pass: 2
diff_base: c4abfc6
head: 09fb684
files_reviewed: 27
files_reviewed_list:
  - src/SocketServer.cpp
  - include/SocketServer.h
  - src/Manager.cpp
  - include/Manager.h
  - src/Events.cpp
  - src/Border.cpp
  - include/Border.h
  - src/Buttons.cpp
  - src/ConfigFileWriter.cpp
  - include/ConfigFileWriter.h
  - include/ConfigProtocol.h
  - apps/wm2-config/main.cpp
  - apps/wm2-config/ProtocolClient.cpp
  - apps/wm2-config/ProtocolClient.h
  - apps/wm2-config/MenuModel.h
  - apps/wm2-ctl/main.cpp
  - CMakeLists.txt
  - scripts/gates/install-components.sh
  - tests/support/WmFixture.h
  - tests/test_config_protocol.cpp
  - tests/test_config_socket.cpp
  - tests/test_config_writer.cpp
  - tests/test_wm2_config_smoke.cpp
  - tests/test_wm_config_live.cpp
  - docs/RELEASE-NOTES.md
  - .planning/WINDOWS.md
  - .planning/phases/09-config-gui-ipc/09-REVIEW.md
first_pass_verdicts:
  closed: 17
  partially_closed: 4
  not_closed: 0
findings:
  critical: 0
  warning: 6
  info: 6
  total: 12
status: issues_found
---

# Phase 09: Code Review Report — Pass 2 (fix verification)

**Reviewed:** 2026-09-06T18:41:01Z
**Depth:** standard
**Diff base:** `c4abfc6..09fb684` (25 fix commits, 27 files, +3252/-196)
**Status:** issues_found

## Summary

Every one of the 21 first-pass items was traced through the code rather than
through its commit message. Nothing is **not closed**. Seventeen are closed
outright; four are **partially closed**, and in each of those four the
first-pass finding's *stated* failure scenario no longer reproduces while a
sibling of it, in the same code, still does. Two of the four partials
(CR-02, WR-12) are the same underlying fact seen from the two clients: the
`reloaded` type is now correlated by the head request's expected type, but
`reloaded` is still correlated **positionally among itself**, so a foreign
broadcast can still be consumed as this client's own reload reply.

The build is clean: `g++ -fsyntax-only -std=c++17 -Wall -Wextra` over
`src/Manager.cpp src/Border.cpp src/Events.cpp src/Buttons.cpp
src/SocketServer.cpp src/ConfigFileWriter.cpp` produces only the four
pre-existing `-Wunused-parameter` warnings in `Border::shapeTabRectangular`
and `Border::setFrameVisibilityRectangular`. The test suite was not run, per
instruction.

Six new findings, none Critical. The two that matter most are structural
rather than scenario-specific: the CR-01 fix's re-entrancy counter is the
only counter in this pass that is **not** RAII (WR-13's `ModalDepthGuard`,
added in the same pass, is), and the WR-02 fix put an **unbounded blocking
`flock`** on the GTK main thread inside the very pass whose WR-09 fix was
about removing unbounded blocking from that thread.

---

## Part 1 — Per-finding verdicts on the 21 first-pass items

| ID | Verdict | The line that convinces |
|---|---|---|
| CR-01 | **closed** | `src/SocketServer.cpp:648-649` — after `handler(req)` at `:643`, `if (index >= m_clients.size()) return; if (m_clients[index].fd != fdAtEntry) return;` before `Connection& after = m_clients[index]`. Backed by `reap()`'s deferral at `:752-755` and the `++/--m_serviceDepth` bracket at `:457`/`:480`. See W-01 for what the bracket does not survive. |
| CR-02 | **partially closed** | `apps/wm2-config/ProtocolClient.cpp:311-314` — `matchesHead` requires `message.type == m_pending.front().expected`, so a `reloaded` can no longer pop a `Value` head. The 23-get/one-notice corruption scenario is dead. Residual: when the head *is* `Reloaded`, correlation is still positional — W-04. |
| CR-03 | **closed** | `src/SocketServer.cpp:264-291` — `::open(m_directory, O_RDONLY\|O_DIRECTORY\|O_NOFOLLOW\|O_CLOEXEC)`, then `fstat`/`fchmod` through `dirFd`; the path string is never handed to `chmod` or `stat` again. See "TOCTOU and EEXIST" below. |
| CR-04 | **closed** (fonts); declined residual re-checked | `src/Manager.cpp:2143-2149` — both `openTabFace` and `openMenuFace` run and can return false *before* `installTabFace`/`installMenuFace` at `:2152-2153`. Probes are freed on every path: `x11::XftFontPtr`'s deleter carries the `Display*` (`include/x11wrap.h:277-283`, set at `:290` and `:322`), so an opened-but-not-installed face is `XftFontClose`d when `newTabFace`/`newMenuFace` leave scope on the refusal return. `installTabFace` `release()`s, so no double close. The declined colour half is re-examined in I-02. |
| WR-01 | **closed** | `src/ConfigFileWriter.cpp:264-268` — `if (ec) { errno = ec.value(); exists = false; return false; }`. The same shape one function away is still missing — I-03. |
| WR-02 | **closed**, new WARNING | `src/ConfigFileWriter.cpp:406` — `const DirectoryLock lock(directory);` is declared before `readLines` at `:412` and destructs after the `rename` at the end of the function, so read and publish are one critical section. No lock file, no stale lock (`flock` dies with the fd). But the acquisition is unbounded and EINTR-lossy — W-02. |
| WR-03 | **partially closed** | `src/ConfigFileWriter.cpp:150-156` refuses `' '`/`'\t'` at either end of `e.value`, and that set exactly matches `trim()`'s (`src/Config.cpp:18-19`, `" \t"` leading / `" \t\r\n"` trailing, with `\r`/`\n` already refused at `:135`). But it guards only `edits`; `menuEntriesAreAcceptable` has no such check — W-03. |
| WR-04 | **closed** | `src/ConfigFileWriter.cpp:590/592` — `failErrno = errno` inside the `write()` arm, the `fsync` arm and the `close` arm; `strerror(failErrno)` at `:596`. The "could not write: Success" spelling is unreachable. |
| WR-05 | **closed** | `src/ConfigFileWriter.cpp:390-396` — `lstat` + `S_ISLNK` returns `WriteFailed` with a sentence naming the link. Note the check precedes the lock (I-04). |
| WR-06 | **closed** | `src/SocketServer.cpp:518-523` sets `m_acceptStalledUntilMs`; `:389` drops `POLLIN` from the listener while it is in force; `:419-423` reports the deadline through `timeoutHintMs()`. Traced the recovery: `nextEvent` rebuilds the poll set every iteration (`src/Events.cpp:275`), so once `nowMs()` passes the deadline `POLLIN` returns and the pending connection is accepted on the next pass — self-healing, one retry per second. `ECONNABORTED`/`EINTR` now `continue` rather than abandoning the batch. |
| WR-07 | **closed** | `src/SocketServer.cpp:782-787` — the bound returns `ForeignWarning::Saturated` once and `Silent` thereafter, so the flood the log-once rule exists to prevent is gone; `warnedUids` still stops growing at 64. |
| WR-08 | **closed** | `src/SocketServer.cpp:170-190` — `SOCK_NONBLOCK`, `poll(POLLOUT, kConfigSocketStaleProbeMs)`, `SO_ERROR`. Every unresolved outcome returns `StaleVerdict::Live`, so T-9-17's never-unlink-what-we-cannot-prove-dead rule is intact. |
| WR-09 | **closed** as specified | `apps/wm2-config/ProtocolClient.cpp:102` (`SOCK_NONBLOCK` from the first syscall), `:111-133` (`EINPROGRESS` → `poll` + `SO_ERROR`), `:224-239` (the EAGAIN arm waits on `POLLOUT` for what is left of the deadline instead of spinning). The residual 5-second bound and the absent reconnect are recorded in I-05. |
| WR-10 | **closed** | `apps/wm2-config/ProtocolClient.cpp:360-377` parses frames *before* reading, so `:383`'s `if (m_in.size() > kConfigProtocolMaxLine)` no longer reasons about newlines; `:389`'s `if (drained >= kProtocolClientMaxDrainPerCallback) return;` bounds the callback at 8192 + one 4096 recv. |
| WR-11 | **closed** | `apps/wm2-config/main.cpp:250-253` — `detachSocketSource()` hangs off the client's state handler, and `ProtocolClient::setState()` (`ProtocolClient.cpp:41-47`) runs on every transition including the one `disconnect()` performs at `:72`, so the `request()`→`send()` failure path is covered. The `m_inSocketCallback` bool is adequate because GLib does not re-dispatch a source that is already being dispatched unless `g_source_set_can_recurse` was called, and it is not. |
| WR-12 | **partially closed** | `apps/wm2-ctl/main.cpp:456` — `if (!configProtocolIsUnsolicitedNotice(reply.type, expected)) break;` fixes the `set`/`get`/`status` cases the finding described. It does nothing for `wm2-ctl reload`, where `expected == Reloaded` — W-05. |
| WR-13 | **closed** | `src/Manager.cpp:2049-2055` refuses `tab-font`/`menu-font`/`frame-thickness` while `m_modalDepth != 0`; `src/Events.cpp:363` installs `ModalDepthGuard` for the whole of `modalWait()`. Checked every `modalWait` caller — `src/Buttons.cpp:165` and `:618`, `src/Border.cpp:1954`, `src/Client.cpp:1833`, `:1950`, `:2480` — all six are inside an `attemptGrab`, so the counter never fires spuriously. Refused, not deferred; there is no deferred apply to be forgotten. The GUI surfaces the refusal (`apps/wm2-config/main.cpp:425-428`, `status("The window manager refused " + k + ": " + reply.reason)`). |
| WR-14 | **closed** | `src/Border.cpp:1963-1969` — `steady_clock` around the `modalWait` call, `tdiff += waited`. The measurement is the right quantity: `modalWait` returns `Timeout` only at `src/Events.cpp:412` (`r == 0 && bounded`), i.e. when the *clamped* poll actually expired, so `waited` is exactly the elapsed clamp. |
| WR-15 | **closed** | `apps/wm2-config/MenuModel.h:105-109` refuses `';'` in name, command and category, and `MenuPage::add()`/`edit()` both reach it through `runEntryDialog` → `entered.complete(reason)` (`apps/wm2-config/MenuPage.cpp:389`), so there is no dialog path that bypasses it. The silent `return` is now `status(...)` at `apps/wm2-config/main.cpp:377-380`. Defence-in-depth gap noted in I-06. |
| WR-16 | **closed** | `src/Buttons.cpp:769-770` — `char string[32]` + `std::snprintf(string, sizeof(string), ...)`; `<cstdio>` is already included at `src/Buttons.cpp:5`. |
| WR-17 | **closed** | `scripts/gates/install-components.sh:57-61` — `case "$BUILD_DIR" in /*) ... exit 2` after `BUILD_DIR` is assigned at `:39` and before any use. Relative `..` forms still work because `$REPO_ROOT/../x` and cmake's `../x` from `cd "$REPO_ROOT"` resolve to the same place. |

### The specific sub-questions asked

**Deferred reaping (`src/SocketServer.cpp`).** *Can a dead connection's fd be
polled or written again before reap?* No. Within a pass, the service loop's
`if (p.fd != m_clients[i].fd) continue;` (`:451`) rejects any connection
`closeConnection()` has set to `fd = -1`, and it runs before `flush()`.
`broadcast()` skips `c.dead` (`:699`). `deliver()` is only reached in
`readConnection` after the two-part re-validation at `:648-649`. Across
passes the question does not arise, because `service()` reaps
unconditionally at `:482` and `appendPollFds()` runs afterwards. *Can the
deferred reap run while a handler still holds a reference?* No: every
`reap()` caller other than `broadcast()` — `expireSilent()` (top of
`service`, depth 0), `dropOldestSilent()` (from `acceptPending`, after the
`--m_serviceDepth` at `:480`) — runs at depth 0 with no handler on the
stack, and `broadcast()`'s is deferred. The one way the bracket fails is an
exception — W-01.

**`reloaded`-as-notice in `ProtocolClient`.** *Does a `reloaded` that IS the
reply to our own `reload` still pop its pending entry?* Yes, exactly once —
but note that the *first* of the two `reloaded` lines the window manager
sends for one `reload` (`src/Manager.cpp:2406` broadcasts, then
`handleConfigRequest` returns a second `reloaded` as the reply; the comment
at `:2400-2402` acknowledges this) is the broadcast, and it is that one which
pops. The second falls to the notice arm. Net effect for the ordinary case:
one handler run, one notice, one refresh — correct. *What if two reloads are
queued?* Both handlers run, in order, against the four lines that come back;
harmless while both reloads have the same outcome, and they always do
because the window manager services both frames from one `readConnection`
loop against one on-disk state. The failure that survives is the
*mixed-origin* case, W-04.

**`O_NOFOLLOW` directory handling.** *TOCTOU between mkdir and open?* Not
exploitable on the paths this project documents. If `mkdir` succeeded we own
a real directory, and removing it from a sticky `/tmp` requires owning it.
If `mkdir` returned `EEXIST` because the attacker had created a real
directory, `fstat`'s `dst.st_uid != ::geteuid()` refuses it (`:275`). *EEXIST
when the existing entry is a file?* `open(..., O_DIRECTORY)` returns
`ENOTDIR`; the refusal is printed with `strerror` and `listen()` returns
false. A FIFO is likewise `ENOTDIR` — Linux checks `LOOKUP_DIRECTORY` before
the FIFO open blocks — so the fallback cannot be made to hang on one either.
Note `O_NOFOLLOW` covers the final component only; an `XDG_RUNTIME_DIR`
pointing through a symlinked parent is still followed, which matches the
finding's scope.

**Probe-then-commit font swap.** No leak on any path. `openTabFace` either
`std::move`s into `out.font` or leaves `out.font` null after the ladder's
three `make_xft_font_*` attempts, each of which is itself an `XftFontPtr`
that closes what it holds when reassigned. `installTabFace` `release()`s
exactly one face and closes exactly one (`src/Border.cpp:660-662`). On the
refusal path (`openMenuFace` false after `openTabFace` true) the probed tab
face is closed by `~XftFontPtr` at the `return false`. `installMenuFace`
takes its argument **by value** (`include/Manager.h:438`) and `release()`s
it, so nothing is closed twice.

**The config-file writer's new lock.** No lock file is created, so there is
no lifetime or staleness problem — `flock` on the parent directory's fd is
released by `close()` in `~DirectoryLock` and by process death. A read-only
directory still admits the lock (`O_RDONLY` needs only `r`+`x`), and the save
then fails later at `mkstemp` with a proper `TempFailed`. Two real problems
remain: unbounded acquisition and a silent EINTR drop — W-02.

**The GTK client's non-blocking send with bounded pending.** Nothing is
dropped. `send()` (`ProtocolClient.cpp:219-241`) retries under `poll(POLLOUT)`
until the 5-second `kHandshakeMs` deadline, then returns false; `request()`
(`:250-253`) turns that into `disconnect(State::NoSocket, ...)`, which fires
the state handler, detaches the GLib source, sets
`ConnectionState::FileOnlyNoSocket` and re-renders. So: a **disconnect with
a visible state**, not dropped frames. The 5 seconds are still a frozen
window, and there is no reconnect (declined in pass 1) — I-05.

**The WR-13 guard.** A `set` is **refused**, not deferred and not applied.
There is no deferred apply, so nothing can be forgotten. A `reload` whose
file moves any of the three keys is refused *whole* while a grab is held,
including every other key in that file — deliberate, documented in
`docs/RELEASE-NOTES.md`, recorded here as I-01.

---

## Part 2 — Are the new tests capable of failing?

Most are. Genuinely falsifiable, with a stated RED that is not a tautology:
`[config_socket][reentrancy]` (asserts the second pipelined frame's reply
arrives — the framing loop losing its place is a real failure, and the ASan
report is the RED), `[config_socket][warn]` (both cases are pure-function
truth tables), `[config_socket][directory]` symlink case (asserts the
victim's mode is still `0755` and no socket node was planted),
`[config_writer][concurrency]` (a pipe handshake makes the interleaving
deterministic rather than raced), `[config_writer][refusal]` whitespace and
symlink cases, `[wm2_config_smoke][protocol][notice]` all three,
`[wm2_config_smoke][protocol][nonblocking]` drain case (`afterOne < sent`
with a stated `SKIP` when the host's buffers cannot hold enough),
`[wm2_config_smoke][menu]`, `[config_protocol][notice]`, and both
`[wm_config_live]` CR-04 cases (the second exists precisely so a guard that
refused *every* two-font reload would not pass the first).

Named exceptions:

1. **`tests/test_config_socket.cpp:747-766`, "A directory owned by somebody
   else is refused" — passes for a reason unrelated to what it names, and
   breaks when run as root.** It sets `XDG_RUNTIME_DIR=/`, so the server
   tries `/wm2-born-again`; as an ordinary user `mkdir` fails with `EACCES`
   and `listen()` returns false at `src/SocketServer.cpp:258` — *before* the
   `fstat` ownership check the case is named after ever runs. The case's own
   comment admits "the mkdir refusal and the ownership refusal are both
   correct outcomes", which is the admission that it does not discriminate.
   It would pass unchanged against the pre-CR-03 code. Worse, as root the
   `mkdir` **succeeds**, `listen()` returns true, `CHECK_FALSE` fails, and
   the run leaves a `/wm2-born-again` directory (with a socket node in it)
   at the filesystem root — see W-06.

2. **`tests/test_wm2_config_smoke.cpp`, "a peer that stops reading does not
   hang the client for ever" — costs ~5 s of wall clock on every run and
   rests on an untested assumption.** The `CHECK(elapsed < 30000)` is sound
   (6× headroom on the 5 s deadline, and the deadline is the thing under
   test), but the loop is bounded at 4000 sends of `kConfigProtocolMaxLine/2`
   bytes ≈ 8 MB; if a host's `SO_SNDBUF` for `AF_UNIX` were tuned above that,
   the loop would exhaust without ever hitting EAGAIN and `CHECK(refused)`
   would fail for an environmental reason with no `SKIP` and no reason. Low
   probability (defaults are ~208 kB), but it is the one new case with an
   unstated environmental precondition.

3. **`tests/test_config_socket.cpp:806-877`, the WR-06 EMFILE case, mutates
   process-global `RLIMIT_NOFILE`.** It is written carefully — the
   assertions are deferred until after the restore, and there is no
   `REQUIRE` between `setrlimit(lowered)` and `setrlimit(original)` that
   could throw past it — so it is not a flake. Flagged only so the next
   editor knows that adding any assertion inside that window would leak a
   64-descriptor limit into every case that runs afterwards.

Nothing in the new tests waits on wall clock for less than 2 s in a way that
could flake under load. The two sleeps that exist —
`tests/test_config_writer.cpp:993` (`usleep(400 ms)`, a *hold* by a child
that the parent blocks on with `flock`, so load makes it slower rather than
racier) and the 10 ms polling loops in `ScriptedPeer` (5000 ms budgets) —
are bounded by handshakes, not by hope. Two caveats on `ScriptedPeer`:
`readLine` increments its `waited` counter on *every* iteration including
successful single-byte reads, so its effective budget is
`timeoutMs/10` **bytes**, not milliseconds — at the 2000 ms it is called
with, that is 200 bytes, comfortably above the ~40-byte `get` frames it
actually reads, but it would silently return `""` on any frame near
`kConfigProtocolMaxLine`. Recorded as I-07 rather than as a live failure.

---

## Part 3 — New findings

No new Critical findings.

## Warnings

### W-01: CR-01's re-entrancy counter is not exception-safe, and a single escaped exception permanently disables the config socket

**File:** `src/SocketServer.cpp:457`, `src/SocketServer.cpp:480`, `src/SocketServer.cpp:749-761`

**Issue:** `service()` brackets the whole servicing loop with two bare
statements:

```cpp
++m_serviceDepth;                       // :457
for (std::size_t i = 0; i < m_clients.size(); ++i) { ... readConnection(i, handler); ... }
--m_serviceDepth;                       // :480
reap();                                 // :482
```

`readConnection` calls `handler(req)` (`:643`), which is
`WindowManager::handleConfigRequest` — a function that builds and copies
`std::string`s, a whole `Config`, and `std::vector<AppEntry>`. If anything in
that call throws, `--m_serviceDepth` is skipped and `m_serviceDepth` is stuck
at ≥ 1 for the life of the process. From then on `reap()` (`:752`) takes the
deferral branch on **every** call and never erases anything:

1. Dead connections accumulate in `m_clients` for ever. Their fds are already
   closed and set to `-1`, so `poll()` ignores them, but the vector never
   shrinks.
2. Once `m_clients.size() >= kConfigSocketMaxClients` (16), `acceptPending()`
   calls `dropOldestSilent()` (`:747`), which finds a dead-but-never-
   hello-completed entry, calls `closeConnection()` on it (a no-op, `fd` is
   already `-1`) and `reap()` (deferred). No slot is freed, so the next line
   `if (m_clients.size() >= kConfigSocketMaxClients) { ::close(fd); continue; }`
   refuses the connection.
3. The configuration socket is permanently deaf: `wm2-ctl` and `wm2-config`
   can connect at the transport level and are then closed without a word,
   for the rest of the session, with no diagnostic.

Reachability today is `std::bad_alloc` and `std::length_error` — every
`std::stoi` in `Config::applyKeyValue` is already wrapped
(`src/Config.cpp:190-200`, `:394-404`, `:416-426`) and `parseWholeInt`
(`src/Manager.cpp:1869-1873`) catches. But `std::bad_alloc` on a 512 MB VPS
running a VNC server is exactly the environment this project names in its
constraints, and the header at `include/SocketServer.h:390-396` sells this
counter as the invariant CR-01 rests on.

The inconsistency is the tell: WR-13, fixed in the same pass, uses RAII for
precisely this pattern (`ModalDepthGuard`, `src/Events.cpp:338-349`, whose
comment says "whichever of modalWait()'s five exits it takes"). The socket
counter has one exit and no guard.

**Fix:** use the guard that already exists in this pass's vocabulary.

```cpp
// SocketServer.cpp, file-local
namespace {
class ServiceDepthGuard {
public:
    explicit ServiceDepthGuard(std::size_t& d) : m_d(d) { ++m_d; }
    ~ServiceDepthGuard() { --m_d; }
    ServiceDepthGuard(const ServiceDepthGuard&) = delete;
    ServiceDepthGuard& operator=(const ServiceDepthGuard&) = delete;
private:
    std::size_t& m_d;
};
}

// service()
{
    const ServiceDepthGuard depth(m_serviceDepth);
    for (std::size_t i = 0; i < m_clients.size(); ++i) { ... }
}   // depth released here, on every exit
reap();
```

A `[config_socket]` case can drive it directly: a handler that throws on the
second frame, then assert `clientCount()` returns to the live count and a
seventeenth connection is still accepted.

---

### W-02: WR-02's fix put an unbounded blocking `flock` on the GTK main thread, and loses the lock silently on EINTR

**File:** `src/ConfigFileWriter.cpp:229-235`, `src/ConfigFileWriter.cpp:406`

**Issue:**

```cpp
explicit DirectoryLock(const std::filesystem::path& directory) {
    m_fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (m_fd < 0) return;
    if (::flock(m_fd, LOCK_EX) != 0) {      // :232 -- no timeout, no retry
        ::close(m_fd);
        m_fd = -1;
    }
}
```

Two problems, both in one call.

**Unbounded.** `configFileWrite()` runs from `ConfigWindow::save()`, i.e.
from a GTK button handler on the main thread. `flock(LOCK_EX)` has no
deadline. Any process that holds an exclusive `flock` on
`~/.config/wm2-born-again` — another `wm2-config` mid-`fsync`, but equally a
backup tool, a dotfile syncer, or a shell doing `flock ~/.config/... -c ...`
— freezes the settings window with no repaint and no way out. That is the
exact failure mode WR-09 was written to remove from this thread, reintroduced
by WR-02 six commits later. The header this pass rewrote
(`apps/wm2-config/ProtocolClient.h:20-27`) now claims "NOTHING HERE EVER
BLOCKS"; the save path beside it does.

**EINTR-lossy.** `flock` is interruptible. A signal delivered during the wait
returns `EINTR`, the constructor closes the fd, `m_fd` stays `-1`, and the
save proceeds **entirely unserialised** — the lost-update WR-02 exists to
prevent, with no diagnostic and no distinguishing result code. "Best effort,
never fatal" is a defensible policy for a filesystem that cannot lock; it
should not also cover a signal.

**Fix:** retry `EINTR`, and bound the wait so a stuck holder degrades to the
pre-WR-02 behaviour rather than to a hung window.

```cpp
explicit DirectoryLock(const std::filesystem::path& directory) {
    m_fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (m_fd < 0) return;

    // Bounded: a save must never be able to hang the window that asked for
    // it. Two seconds is far longer than any honest holder needs and far
    // shorter than a user will wait before killing the program.
    const auto until = std::chrono::steady_clock::now() +
                       std::chrono::seconds(2);
    for (;;) {
        if (::flock(m_fd, LOCK_EX | LOCK_NB) == 0) return;   // held
        if (errno == EINTR) continue;                        // not a refusal
        if (errno != EWOULDBLOCK) break;                     // cannot lock here
        if (std::chrono::steady_clock::now() >= until) break;
        ::usleep(20 * 1000);
    }
    ::close(m_fd);
    m_fd = -1;
}
```

If the bound is reached, the honest outcome is arguably a new
`ConfigWriteResult::Busy` the GUI reports as "another program is writing this
file; try again" — the writer already has five other result codes that exist
to say exactly this kind of thing.

---

### W-03: WR-03's round-trip guard covers single-key edits and not menu entries, so a menu entry name with an outer space still does not round-trip

**File:** `src/ConfigFileWriter.cpp:146-169` (`menuEntriesAreAcceptable`), `apps/wm2-config/MenuModel.h:79-118` (`MenuEntryDraft::complete`)

**Issue:** WR-03's fix is in `editsAreAcceptable` (`:150-156`) and guards
`ConfigEdit::value`. Menu entries take a different route to the same file
format: `appendMenuEntry` (`src/ConfigFileWriter.cpp:200-205`) writes
`menu-entry-name = <name>`, `menu-entry-command = ...`,
`menu-entry-category = ...`, and `Config::applyFile` reads them back through
the same `trim(value)` at `src/Config.cpp:127`. `menuEntriesAreAcceptable`
checks only for `\n`, `\r` and length; `MenuEntryDraft::complete()` checks
emptiness, `';'` and length. Neither refuses an outer space.

The asymmetry that makes this a defect rather than a cosmetic nit is that the
**wire** path does not trim: `parseMenuEntriesValue` calls
`scratch.applyKeyValue(key, val)` directly (`src/Config.cpp:842`), bypassing
`applyFile`'s `trim`. So:

1. The user types a menu entry named `" Mail"` (a leading space is trivially
   produced by a paste, and `complete()` accepts it because
   `" Mail".empty()` is false).
2. `commitRows` → `applyLive` → `set menu-entries ...`. The window manager's
   round-trip check (`src/Manager.cpp:2199-2205`) passes, because render and
   parse both preserve the space. The live menu shows `" Mail"`.
3. The user presses Save. The file gets `menu-entry-name =  Mail`.
4. Anything that reloads — `wm2-ctl reload`, the next login — reads it back
   as `"Mail"`. `get menu-entries` now disagrees with what the window manager
   held a moment ago, and `FormState::adoptEffectiveMenuEntries` silently
   rewrites the user's row.

This is WR-03's own sentence — "the running window manager holds one string,
the file says another" — reached through the menu-entry keys instead of
through `new-window-command`.

**Fix:** the same refusal, in both places, spelled once.

```cpp
// src/ConfigFileWriter.cpp, inside menuEntriesAreAcceptable's per-part loop
if (!parts[i]->empty() &&
    (parts[i]->front() == ' ' || parts[i]->front() == '\t' ||
     parts[i]->back()  == ' ' || parts[i]->back()  == '\t')) {
    errorOut = std::string("the menu entry ") + names[i] +
               " begins or ends with a space, which the configuration file "
               "cannot preserve";
    return false;
}
```

and the matching arm in `MenuEntryDraft::complete()`'s existing
`for (const std::string* part : {&name, &command, &category})` loop, so the
dialog says it where it was typed — the reason IN-05 gave for putting the
length bound there.

---

### W-04: CR-02 correlates `reloaded` positionally among itself, so a refused reload can be reported to the user as a success

**File:** `apps/wm2-config/ProtocolClient.cpp:311-334`

**Issue:** `matchesHead` pops the head whenever the arriving type equals the
head's `expected`. For `Value`/`Ack` heads that is a real correlation,
because only one message type can answer them. For a `Reloaded` head it is
not: `reloaded` is D-08's broadcast *and* the reply, and the client cannot
tell whose reload produced the line it just read.

Concrete failure scenario:

1. The user presses "Re-read files". `sendReload` pushes
   `Pending{expected = Reloaded, handler}` (`ProtocolClient.cpp:291`).
2. Before the window manager services that frame, some other client's reload
   completes — a `wm2-ctl reload` in a login script, a second settings window,
   the `wm2-ctl reload` a packaging hook runs. Its broadcast
   (`src/Manager.cpp:2406`) lands on this connection.
3. `matchesHead` is true (`Reloaded == Reloaded`). Our pending entry is
   popped and its handler is run with the foreign notice. The handler
   (`apps/wm2-config/main.cpp:483-488`) checks only for
   `ConfigMessageType::Error`, so it does nothing — silently.
4. *Our* reload then fails — the user's config file was made unreadable, or
   carries a colour the X server refuses, which is
   `reloadConfigFromDisk`'s documented refusal. The window manager answers
   `error`.
5. `m_pending` is now empty, so `matchesHead` is false and
   `configProtocolIsUnsolicitedNotice(Error, Unknown)` is false: the `error`
   is **dropped without a word** (`:329`, the bare `return`). The user
   pressed "Re-read files", it failed, and the settings window says nothing
   — or, worse, shows the "The window manager re-read its configuration
   files" banner from step 2's second line.

The header's new claim at `ProtocolClient.h:79-83` — "A line that matches no
outstanding request is a notice or is dropped" — is where the hole is: an
`error` that matches no outstanding request is neither, and dropping it is
how a refusal becomes a silence.

**Fix:** two independent halves, both small.

```cpp
// 1. Never drop an error. If it matches no head it is still a refusal of
//    something this client asked for, and the user has to be told.
if (!matchesHead) {
    if (configProtocolIsUnsolicitedNotice(message.type, expected)) {
        if (m_onNotice) m_onNotice();
    } else if (message.type == ConfigMessageType::Error && m_onProtocolError) {
        m_onProtocolError(message.reason);   // -> status(), not silence
    }
    return;
}
```

```cpp
// 2. Give `reload` a correlator so the two `reloaded` lines can be told
//    apart. The cheapest one that changes no wire bytes: count outstanding
//    reloads, and treat the FIRST reloaded after a reload was sent as the
//    reply only if the window manager has not also broadcast in between --
//    or, more honestly, have reloadConfigFromDisk() NOT broadcast to the
//    connection it is answering, since the comment at src/Manager.cpp:2400
//    already observes that "both say the same thing".
```

The second half is the smaller change and closes the ambiguity at its source
for both clients: `broadcast()` gains an "except this connection" parameter,
the requester gets exactly one `reloaded` (its reply), and W-05 disappears
with it.

---

### W-05: `wm2-ctl reload` still cannot tell a foreign notice from its own reply, and exits 0 for a reload the window manager refused

**File:** `apps/wm2-ctl/main.cpp:440-456`, `include/ConfigProtocol.h:220-224`

**Issue:** the WR-12 fix skips a `reloaded` only when
`expected != ConfigMessageType::Reloaded`:

```cpp
inline bool configProtocolIsUnsolicitedNotice(ConfigMessageType arrived,
                                              ConfigMessageType expected) {
    return arrived == ConfigMessageType::Reloaded &&
           expected != ConfigMessageType::Reloaded;
}
```

For `wm2-ctl reload`, `expected` **is** `Reloaded`, so the loop at `:449-456`
breaks on the first `reloaded` line it sees, whoever caused it.

Concrete failure: a provisioning script runs
`wm2-ctl reload && systemctl --user restart something`. A second reload —
from a settings window, or from the previous line of the same script if it
backgrounded one — completes first and broadcasts. `wm2-ctl` reads that
broadcast, `reportReply` sees `Reloaded == expected`, and it exits **0**.
Its own reload then fails (unreadable file, unparseable colour) and the
`error` is never read. The script takes the success branch after a reload
that did not happen — the mirror image of the finding WR-12 was written
about, and with the same exit-code contract at `apps/wm2-ctl/main.cpp:21-29`
being violated in the other direction.

WR-12's fix is correct for the four other `expected` values and this is
strictly the case it could not reach with a type-only classifier, which is
why the verdict above is *partially* closed rather than *not* closed.

**Fix:** the same one W-04's second half names — stop broadcasting to the
connection being answered, so a `reloaded` on a `reload` requester's socket
is unambiguously its own reply:

```cpp
// src/Manager.cpp, reloadConfigFromDisk()
// The requester gets its own `reloaded` REPLY from handleConfigRequest and
// must not also get the broadcast: the two are indistinguishable on the
// wire, so sending both makes "is this mine?" unanswerable for every client
// (W-04, W-05).
m_socketServer.broadcastExcept(configProtocolEncode(notice), m_servingFd);
```

`ConfigSocketServer` already knows which connection it is serving — the
index `readConnection` now carries — so the parameter is available without
new plumbing.

---

### W-06: the CR-03 ownership test proves nothing, and corrupts the filesystem root when the suite runs as root

**File:** `tests/test_config_socket.cpp:747-766`

**Issue:**

```cpp
RuntimeDirEnv::set("/");
ConfigSocketServer server;
CHECK_FALSE(server.listen(":notmine"));
CHECK_FALSE(server.isListening());
```

`configSocketDirectory()` returns `"//wm2-born-again"`. As an ordinary user
`::mkdir` fails with `EACCES` and `listen()` returns false at
`src/SocketServer.cpp:258` — the `open`/`fstat`/`fchmod` block the case is
named after is never entered. The case would pass identically against the
pre-CR-03 `mkdir`/`chmod`/`stat` sequence, which is the definition of an
assertion that proves nothing. The case's own comment concedes this
("both are correct outcomes and both are 'no socket'").

Run as root — a container, a CI image, `sudo ctest`, any of which this
project's own release-evidence workflow makes plausible — `mkdir
/wm2-born-again` **succeeds**, the `fstat` uid check passes (root owns it),
`fchmod` sets `0700`, `bind` creates `/wm2-born-again/socket_notmine`, and
`listen()` returns **true**. `CHECK_FALSE` fails, and the run leaves a
directory and a socket node at the filesystem root that nothing cleans up:
there is no `rmdir`/`unlink` in this case, unlike the two `[directory]` cases
either side of it.

**Fix:** drive the check it is named after, and clean up whatever it makes.

```cpp
TEST_CASE("A directory owned by somebody else is refused",
          "[config_socket][directory]")
{
    // /tmp: this process does not own it, and it exists, so mkdir returns
    // EEXIST and the FSTAT is what refuses -- which is the check under test.
    RuntimeDirEnv guard;
    TempDir home;
    REQUIRE(home.valid());

    // A directory owned by somebody else, at exactly the name listen() will
    // use. Skipped rather than faked when the case cannot become another
    // user, so it never passes vacuously.
    if (::geteuid() == 0) {
        SKIP("running as root: every directory is owned by this user, so the "
             "ownership refusal cannot be reached");
    }
    RuntimeDirEnv::set("/var");        // exists, root-owned, not 0700
    ConfigSocketServer server;
    CHECK_FALSE(server.listen(":notmine"));
    CHECK_FALSE(server.isListening());
    // Nothing to remove: the refusal is asserted to have created nothing.
    struct stat st;
    CHECK(::lstat("/var/wm2-born-again", &st) != 0);
}
```

---

## Info

### I-01: a reload during any modal grab is now refused whole, including the keys it could have applied

**File:** `src/Manager.cpp:2049-2055`

The WR-13 guard sits above the colour pre-flight and above `m_config = next`,
and `reloadConfigFromDisk` applies a whole `Config`. So a config file that
changes `frame-thickness` *and* six colours, reloaded while the root menu is
open, applies none of the six. This is deliberate (the disposition's
"refused, not deferred") and documented in `docs/RELEASE-NOTES.md`, and the
alternative — applying the safe half of a file and refusing the rest —
would be worse. Recorded so the next reader does not mistake it for an
oversight: the sentence the client receives ("a menu or a drag is in
progress; try again in a moment") is about a `set`, and for a `reload` it
under-describes what was refused.

### I-02: CR-04's declined colour residual rests on an inaccurate premise

**File:** `.planning/phases/09-config-gui-ipc/09-REVIEW.md` (CR-04 disposition), `src/Manager.cpp:2077-2099`, `src/Border.cpp:608-680`, `src/Manager.cpp:1930-1960`

The disposition declines splitting `Border::reloadColours()` /
`reloadMenuColours()` because "the only way to reach a half-applied palette
is an allocation that fails after an identical one succeeded". It is not
identical. The pre-flight resolves all nine names through
`tryAllocateColour` (`src/Manager.cpp:2084`); `reloadMenuColours` then
resolves three of them through `x11::XftColorWrap` →
`XftColorAllocName` (`src/Manager.cpp:1938-1944`), a different allocator on a
different code path. `Border::reloadColours` additionally allocates two Xft
colours, two derived shades (`tryAllocateShadeOf`, `src/Border.cpp:657-658`)
and up to three GCs, none of which the pre-flight touches.

The conclusion still holds on the TrueColor visuals this project targets —
both routes reduce to parsing a name and computing a pixel, and
`Border::reloadColours` is allocate-then-swap so its own failures commit
nothing. So this is a note on the *reasoning*, not a live defect: the
invariant is held by the visual class, not by the pre-flight, and the comment
at `src/Manager.cpp:2081-2083` should say so.

### I-03: `std::filesystem::exists`'s `error_code` is still ignored one function above WR-01's fix

**File:** `src/ConfigFileWriter.cpp:368`

```cpp
if (!std::filesystem::exists(directory, ec)) {
    std::filesystem::create_directories(directory, ec);
    ...
}
```

Same shape WR-01 fixed at `:264`. Here the consequence is benign — an
`EACCES` on a path component makes `exists` return false, `create_directories`
then fails and reports `DirectoryFailed` — but the two spellings of the same
call now disagree about whether a failed existence check is a failure, in one
file, thirty lines apart.

### I-04: the WR-05 symlink check runs before the lock, so it is advisory only

**File:** `src/ConfigFileWriter.cpp:388-397` vs `:406`

`lstat` is at `:391`, `DirectoryLock` at `:406`. A second saver that respects
the lock cannot race it (it would have to create the symlink between our
`lstat` and its own lock acquisition, and it does not create symlinks); a
hand edit can. Since WR-05 was explicitly about ending a *silence* rather
than about defeating an attacker, this is a note, not a defect. Moving the
check below the lock costs nothing and removes the question.

### I-05: `ProtocolClient::send()` can still freeze the settings window for five seconds, then leave it permanently file-only

**File:** `apps/wm2-config/ProtocolClient.cpp:218-241`, `apps/wm2-config/main.cpp:234`

The WR-09 fix is exactly the shape the finding asked for, and the freeze is
now bounded. What it is bounded *to* is `kHandshakeMs` = 5000 ms, from a GTK
signal handler, with no repaint; and when it expires the window disconnects
for good, because the reconnect was declined in pass 1 as a feature request.
So the worst case a user meets is: five seconds of a dead window, followed by
a settings program that is file-only until it is restarted, from a window
manager that was merely slow. Worth a smaller write deadline (the window
manager services the socket from `modalWait` on every iteration, so a healthy
peer never needs seconds) and worth the reconnect the declined half asked
for.

### I-06: `';'` is refused in the dialog and nowhere else

**File:** `apps/wm2-config/MenuModel.h:105-109`, `src/ConfigFileWriter.cpp:146-169`

`MenuEntryDraft::complete()` is the only guard. `menuEntriesAreAcceptable`
does not refuse `';'`, so an entry that reaches the writer by any other route
still puts one in the file. The route that exists today is a hand-edited
config file: `menu-entry-name = Mail; News` is read by the window manager,
returned by `get menu-entries`, and split by `parseMenuEntriesValue` into
`menu-entry-name=Mail` and ` News` — the second has no `=`, so the parse
fails and (thanks to WR-15's other half) the user is now told. A name of the
form `Foo;menu-entry-category=Bar` would instead parse *successfully* into
something the user did not write. Cheap to close: the same `find(';')` in
`menuEntriesAreAcceptable`.

### I-07: `ScriptedPeer::readLine` budgets iterations, not milliseconds

**File:** `tests/test_wm2_config_smoke.cpp:2677-2691`

```cpp
for (int waited = 0; waited < timeoutMs; waited += 10) {
    ... if (n == 1) { line += c; if (c == '\n') return line; continue; }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
```

`waited += 10` runs on every iteration, including the ones that consumed a
byte without sleeping, so the effective budget is `timeoutMs / 10` **bytes**.
At the 2000 ms every call site passes, that is 200 bytes; the `get` and `set`
frames it reads are ~40, so no current case is affected. A future case that
reads a `value` reply carrying a `menu-entries` list would get a spurious
`""` and a misleading `REQUIRE_FALSE(...empty())` failure. Advance `waited`
only on the sleep path.

### I-08: `m_reapPending` is written and never read, and the header describes a mechanism the code does not use

**File:** `include/SocketServer.h:396`, `src/SocketServer.cpp:753`, `src/SocketServer.cpp:760`

`include/SocketServer.h:392-395` says "reap() therefore DEFERS while this is
non-zero and **records that it owes one**, and service() pays the debt once".
`service()` calls `reap()` unconditionally at `:482` and never consults
`m_reapPending`; the flag is set at `:753` and cleared at `:760` and read
nowhere. The behaviour is correct — the unconditional call is strictly
stronger than a conditional one — but the field is dead state whose
documentation asserts a correlation that does not exist, which is the same
complaint IN-01 made about `Pending::expected` last pass. Either drop the
field or make `service()` use it.

### I-09: `include/ConfigFileWriter.h` uses `std::size_t` without including `<cstddef>`

**File:** `include/ConfigFileWriter.h:70`

`inline constexpr std::size_t kConfigFileMaxValueBytes = 256;` with only
`<string>` and `<vector>` above it. It compiles everywhere today because both
drag `<cstddef>` in transitively, and `apps/wm2-config/MenuModel.h` now
includes this header for that constant. One line.

---

_Reviewed: 2026-09-06T18:41:01Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard, pass 2_
