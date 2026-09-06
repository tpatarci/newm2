---
phase: 09-config-gui-ipc
reviewed: 2026-09-06T16:43:45Z
depth: standard
diff_base: 0fec5db
files_reviewed: 52
files_reviewed_list:
  - src/SocketServer.cpp
  - include/SocketServer.h
  - src/Manager.cpp
  - include/Manager.h
  - src/Events.cpp
  - src/Config.cpp
  - include/Config.h
  - include/ConfigProtocol.h
  - include/ConfigFileWriter.h
  - src/ConfigFileWriter.cpp
  - src/Border.cpp
  - include/Border.h
  - src/Buttons.cpp
  - src/Client.cpp
  - include/Client.h
  - src/AppCache.cpp
  - include/AppCache.h
  - src/DesktopEntry.cpp
  - include/DesktopEntry.h
  - include/RootMenuModel.h
  - src/main.cpp
  - apps/wm2-ctl/main.cpp
  - apps/wm2-config/main.cpp
  - apps/wm2-config/ProtocolClient.cpp
  - apps/wm2-config/ProtocolClient.h
  - apps/wm2-config/ConnectionState.h
  - apps/wm2-config/FormState.cpp
  - apps/wm2-config/FormState.h
  - apps/wm2-config/MenuModel.h
  - apps/wm2-config/AppearancePage.cpp
  - apps/wm2-config/AppearancePage.h
  - apps/wm2-config/BehaviourPage.cpp
  - apps/wm2-config/BehaviourPage.h
  - apps/wm2-config/MenuPage.cpp
  - apps/wm2-config/MenuPage.h
  - CMakeLists.txt
  - packaging/wm2-born-again.desktop
  - packaging/wm2-config.desktop
  - scripts/gates/build-all.sh
  - scripts/gates/doc-keys.sh
  - scripts/gates/install-components.sh
  - scripts/preflight.sh
  - scripts/capture-display-capabilities.sh
  - tests/support/WmFixture.h
  - tests/test_config_protocol.cpp
  - tests/test_config_writer.cpp
  - tests/test_config_socket.cpp
  - tests/test_wm_socket.cpp
  - tests/test_wm_config_live.cpp
  - tests/test_wm2_config_smoke.cpp
  - tests/test_desktopentry.cpp
  - tests/test_menupaint.cpp
findings:
  critical: 4
  warning: 17
  info: 8
  total: 29
status: issues_found
---

# Phase 09: Code Review Report

**Reviewed:** 2026-09-06T16:43:45Z
**Depth:** standard
**Diff base:** 0fec5db
**Files Reviewed:** 52
**Status:** issues_found

## Summary

This phase adds a Unix-socket IPC surface to a single-threaded window manager, a
surgical config-file writer, a live-apply funnel, and a GTK client. The code is
unusually well documented and most of the obvious hazards (line bounds, peer-uid
check, allocate-then-swap palettes, atomic rename, non-blocking descriptors) are
handled deliberately. The defects that remain are all in the seams **between**
the well-guarded pieces:

- **Re-entrancy.** The socket server calls a handler that calls back into the
  socket server. `broadcast()` reaps the connection vector while
  `readConnection()` still holds a reference into it (CR-01). Nothing in the
  transport documents that the handler may not re-enter, and the one handler
  that exists does exactly that.
- **Reply correlation.** The design deliberately reuses `reloaded` as both a
  reply and an unsolicited broadcast, but neither client distinguishes them. The
  GTK client's FIFO dispatch desynchronises and writes values under the wrong
  keys into the user's config file (CR-02).
- **Path trust.** The socket-node handling is careful about symlinks
  (`lstat`, `S_ISSOCK`, never unlink what we did not create); the *directory*
  handling on the documented `/tmp` fallback is not (CR-03).
- **Half-applied state.** `applyConfig()`'s own comment argues the two-stage
  font swap is safe. For a `reload` it is not (CR-04).

The Priority-1..5 areas in the brief were each driven to a concrete failure
scenario. Test sources were scanned for reliability hazards rather than read
line by line (the suite was not run, per instruction).

---

## Critical Issues

### CR-01: Use-after-free — the request handler reaps the connection vector it is being called from

**File:** `src/SocketServer.cpp:454-508` (`readConnection`), `src/SocketServer.cpp:546-554` (`broadcast`), `src/SocketServer.cpp:593-598` (`reap`), `src/Manager.cpp:2335` (`reloadConfigFromDisk`)

**Issue:**
`readConnection()` holds `Connection& c`, a reference into `m_clients`, across
the handler call:

```cpp
const ConfigSocketReply reply = handler(req);        // :501  -- may reap m_clients
if (reply.helloAccepted) c.helloSeen = true;         // :502
if (!reply.line.empty() || reply.closeAfterSend) {
    deliver(c, reply.line, reply.closeAfterSend);    // :504
}
if (c.closing || c.dead) return;                     // :506
```

The handler is `WindowManager::handleConfigRequest`. For a `reload` request it
reaches `reloadConfigFromDisk()`, which calls
`m_socketServer.broadcast(configProtocolEncode(notice))` (`src/Manager.cpp:2335`).
`broadcast()` ends with `reap()` (`src/SocketServer.cpp:553`), and `reap()` is
`erase(remove_if(...), end())` — it destroys and shifts elements of `m_clients`.

`broadcast()` marks a connection dead whenever `deliver()` overflows
`kConfigSocketMaxPending` or `flush()`'s `send()` fails with EPIPE/ECONNRESET
(`src/SocketServer.cpp:514-517`, `:539`). Concrete failure scenario:

1. Client B connects and completes the handshake, then dies (`kill -9`) or stops
   reading. Its `Connection` is still in `m_clients` at index 0.
2. Client A (a `wm2-config` window, or `wm2-ctl reload`) is at index 1 and sends
   `reload`.
3. `service()` → `readConnection(m_clients[1], ...)` → `handler` →
   `reloadConfigFromDisk` → `broadcast` → `send()` to B fails →
   `closeConnection(B)` → `reap()` erases index 0.
4. `m_clients` is now size 1. The reference `c` still names index 1, which is
   one past the new end — the `Connection` there (and its two `std::string`
   members) has been destroyed.
5. Control returns to `readConnection`, which reads `c.closing`, calls
   `deliver(c, ...)` (`c.out += line` on a freed `std::string`), and re-enters the
   framing loop on `c.in`. Heap use-after-free / heap corruption in the window
   manager's only thread.

Even when `c` is not the last element the bug is live: after the shift, `c`
names a *different* connection, so the `reloaded` reply is written to the wrong
peer and `c.helloSeen`/`c.closing` are read and written on the wrong connection.

The header's contract (`include/SocketServer.h:247-253`) says nothing about the
handler being forbidden to re-enter, and the one handler that exists re-enters.

**Fix:** make the transport re-entrancy-safe rather than relying on a handler
contract. Index instead of holding a reference, and re-validate after the
handler returns; and make `reap()` a no-op while a service pass is in flight.

```cpp
// SocketServer.h
std::size_t m_serviceDepth = 0;   // reap() defers while non-zero

// SocketServer.cpp
void ConfigSocketServer::reap()
{
    if (m_serviceDepth != 0) { m_reapPending = true; return; }
    m_clients.erase(std::remove_if(m_clients.begin(), m_clients.end(),
                                   [](const Connection& c) { return c.dead; }),
                    m_clients.end());
    m_reapPending = false;
}

// readConnection: take the index, not the reference, and re-look-up after the
// handler so a shift or an erase cannot be read through a stale reference.
void ConfigSocketServer::readConnection(std::size_t index, const Handler& handler)
{
    // ... recv into m_clients[index].in as before ...
    for (;;) {
        Connection& c = m_clients[index];          // re-taken every iteration
        // ... frame extraction ...
        ++m_serviceDepth;
        const ConfigSocketReply reply = handler(req);
        --m_serviceDepth;
        if (index >= m_clients.size() || m_clients[index].fd != fdAtEntry) return;
        Connection& after = m_clients[index];
        if (reply.helloAccepted) after.helloSeen = true;
        // ...
    }
}
```

Whatever shape is chosen, add a `[config_socket]` case that drives it: two
connections, the first one's peer closed, the second one sending `reload`. It
fails under ASan today.

---

### CR-02: wm2-config correlates replies positionally and adopts values under the wrong key, then writes them to the user's config file

**File:** `apps/wm2-config/ProtocolClient.cpp:251-266` (`dispatch`), `apps/wm2-config/ProtocolClient.cpp:202-212` (`request`), `apps/wm2-config/main.cpp:284-299` (`readEffectiveValuesFromWindowManager`)

**Issue:**
`Pending` carries an `expected` message type (`ProtocolClient.h:117`) and
`request()` stores it (`ProtocolClient.cpp:210`) — but `dispatch()` never reads
it. It pops the head of the queue for *any* decodable message:

```cpp
if (m_pending.empty()) {
    if (message.type == ConfigMessageType::Reloaded && m_onNotice) m_onNotice();
    return;
}
Pending p = std::move(m_pending.front());
m_pending.pop_front();
if (p.handler) p.handler(message);
```

The window manager deliberately injects an **unsolicited** `reloaded` into the
same stream (`src/Manager.cpp:2320-2335`, D-08), sent to every hello-completed
connection. So the stream is not a pure request/response sequence, and the
header's claim that "a reply is delivered exactly once, in the order the
requests were sent" (`ProtocolClient.h:57-60`) does not hold.

The per-key handler compounds it — it never checks `reply.key`:

```cpp
m_client.sendGet(k, [this, k](const ConfigMessage& reply) {
    if (reply.type != ConfigMessageType::Value) return;
    ...
    m_form.adoptEffective(k, reply.value, ValueSource::WindowManager, std::string());
```

Concrete failure scenario (trivially reproducible, no race window needed to be
narrow):

1. `wm2-config` starts. `readEffectiveValuesFromWindowManager()` +
   `readMenuCategoriesFromWindowManager()` push **23** `Pending` entries in one
   main-loop turn (`main.cpp:285-309`, `:317-324`).
2. Anything triggers a reload elsewhere — `wm2-ctl reload`, a second
   `wm2-config` pressing "Re-read files", a script doing
   `wm2-ctl reload; wm2-ctl reload`.
3. The `reloaded` broadcast arrives on this connection first. `m_pending` is not
   empty, so it pops the `tab-foreground` get and hands it a `reloaded` message.
   That handler returns early (`type != Value`) — silently.
4. Every subsequent `value` reply is now off by one. The `tab-background` reply
   is handed to the `tab-foreground` handler, which does **not** check
   `reply.key`, and calls `adoptEffective("tab-foreground", <the background
   colour>, ...)`. And so on down the list — colours into font fields, integers
   into colour fields.
5. `adoptEffective` sets `field->current = value` for every non-dirty field
   (`FormState.cpp:127-130`), so the window now shows wrong values. The user
   presses Save; `configFileWrite` writes them to
   `~/.config/wm2-born-again/config`. **The user's configuration file is
   corrupted with values they never entered.**

A second, milder consequence of the same bug: a `reloaded` broadcast that lands
while any request is outstanding is consumed as that request's reply, so
`m_onNotice()` never fires and the window silently misses D-08's whole point.

**Fix:** match on the head's `expected` type before popping, and treat a
non-matching `reloaded` as a notice. Additionally, make the per-key handlers
assert the key.

```cpp
void ProtocolClient::dispatch(const ConfigMessage& message)
{
    // An unsolicited notice is told apart by TYPE, not by queue depth: the
    // window manager injects `reloaded` into a stream that may already have
    // requests in flight (D-08).
    const bool matchesHead =
        !m_pending.empty() &&
        (message.type == m_pending.front().expected ||
         message.type == ConfigMessageType::Error);

    if (!matchesHead) {
        if (message.type == ConfigMessageType::Reloaded && m_onNotice) m_onNotice();
        return;
    }

    Pending p = std::move(m_pending.front());
    m_pending.pop_front();
    if (p.handler) p.handler(message);
}
```

```cpp
// main.cpp:288 -- belt to the transport's braces
m_client.sendGet(k, [this, k](const ConfigMessage& reply) {
    if (reply.type != ConfigMessageType::Value) return;
    if (reply.key != k) return;          // never adopt a value under the wrong key
    ...
```

Note that `Error` still has to be accepted for any head (the window manager
answers `get`/`set` refusals with `error`), which is why it is spelled
explicitly above.

---

### CR-03: Socket directory is created and chmod'ed through an unvalidated symlink on the documented /tmp fallback

**File:** `src/SocketServer.cpp:87-96` (`configSocketDirectory`), `src/SocketServer.cpp:210-234` (`listen`)

**Issue:**
When `XDG_RUNTIME_DIR` is unset or not absolute, the directory is
`/tmp/wm2-born-again-<uid>` (`:95`) — a path in a world-writable directory whose
name is predictable from the uid. `listen()` then does:

```cpp
if (::mkdir(m_directory.c_str(), 0700) != 0 && errno != EEXIST) { ... }   // :214
if (::chmod(m_directory.c_str(), 0700) != 0) { ... }                      // :220
struct stat dst;
if (::stat(m_directory.c_str(), &dst) != 0 || !S_ISDIR(dst.st_mode) ||
    dst.st_uid != ::geteuid() || (dst.st_mode & 07777) != 0700) { ... }   // :227-234
```

`mkdir` on an existing symlink-to-directory returns `EEXIST`, and both `chmod`
and `stat` **follow symlinks**. Every check therefore inspects the symlink's
*target*, not the path the socket will live under.

Concrete failure scenario (attacker uid B, victim uid 1000, no
`XDG_RUNTIME_DIR` — which is the ordinary state under `su`, a bare
`startx`/`xinit`, and several minimal VNC session scripts this project targets):

1. Before the victim's window manager has ever run, B creates
   `/tmp/wm2-born-again-1000` as a symlink to `/home/victim/public_html` (or
   `/home/victim`, or any directory the victim owns). The sticky bit on `/tmp`
   permits creating a *new* name.
2. The victim's window manager starts. `mkdir` → `EEXIST`. `chmod(path, 0700)`
   follows the symlink and **succeeds**, because the victim owns the target — it
   silently changes that directory to mode 0700. `stat` follows the symlink,
   sees a victim-owned 0700 directory, and every check passes.
3. `configSocketStaleVerdict()` lstats `/tmp/wm2-born-again-1000/socket_0`,
   which resolves inside the victim's chosen-by-the-attacker directory, gets
   `NoFile`, and `bind()` creates a socket node there.

Net effect: an attacker-directed `chmod 0700` on an arbitrary victim-owned
directory (breaking a web root, a shared group directory, a Maildir), plus a
socket node planted in it. The socket-node path is careful about exactly this
class of trick (`configSocketStaleVerdict` uses `lstat`, requires `S_ISSOCK`,
and refuses to unlink anything it did not create — `:152-176`, T-9-17). The
directory path is not, and that asymmetry is what makes this a defect rather
than an accepted risk.

**Fix:** never operate on the directory by name after the existence check. Open
it with `O_NOFOLLOW` and work through the descriptor:

```cpp
if (::mkdir(m_directory.c_str(), 0700) != 0 && errno != EEXIST) { ...warn...; return false; }

const int dirFd = ::open(m_directory.c_str(),
                         O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
if (dirFd < 0) {
    std::fprintf(stderr, "wm2: warning: %s is not a directory this process may "
                         "use (%s), no configuration socket\n",
                 m_directory.c_str(), std::strerror(errno));
    return false;                       // ELOOP here IS the symlink refusal
}
struct stat dst;
if (::fstat(dirFd, &dst) != 0 || !S_ISDIR(dst.st_mode) ||
    dst.st_uid != ::geteuid()) {
    ::close(dirFd);
    std::fprintf(stderr, "wm2: warning: %s is not a directory owned by this "
                         "user, no configuration socket\n", m_directory.c_str());
    return false;
}
if ((dst.st_mode & 07777) != 0700 && ::fchmod(dirFd, 0700) != 0) {
    ::close(dirFd); /* warn */ return false;
}
::close(dirFd);
```

`O_NOFOLLOW` on the final component turns the symlink case into `ELOOP`, which
is the refusal this path is missing. (Under `XDG_RUNTIME_DIR` the exposure is
smaller because that directory is itself 0700-owned, but the fallback is a
documented, supported path — D-16 — and must not be weaker.)

---

### CR-04: applyConfig leaves the tab face swapped when the menu font is refused — a permanent half-applied state that `get` denies

**File:** `src/Manager.cpp:2064-2078`, `src/Manager.cpp:2090`, `src/Manager.cpp:2121-2126`; `src/Border.cpp:600-663` (`Border::reloadTabFont`)

**Issue:**

```cpp
const bool tabFontChanged  = next.tabFont  != previous.tabFont;   // :2064
const bool menuFontChanged = next.menuFont != previous.menuFont;  // :2065

if (tabFontChanged && !Border::reloadTabFont(this, next.tabFont)) {
    reasonOut = "no usable face for that tab-font pattern";
    return false;
}
if (menuFontChanged && !reloadMenuFont(next.menuFont)) {
    reasonOut = "no usable face for that menu-font pattern";
    return false;                                                  // :2072
}
m_config = next;                                                   // :2090
```

`Border::reloadTabFont()` is not a validation — it commits. It closes the old
`XftFont`, installs the new one, and **recomputes `m_tabWidth` and
`m_tabBaseline`** (`src/Border.cpp:637-661`). If `reloadMenuFont()` then fails,
the function returns `false` at `:2072` having already changed the shared tab
face and the tab geometry constant, while `m_config` is never updated.

The comment at `:2073-2079` argues this is safe: *"a `set` names ONE key, so at
most one of these two branches ever runs for a set, and a `reload` that fails
here refuses whole and leaves the window manager on the configuration it already
had"*. The second half is false. A `reload` applies a whole `Config` from disk,
and a file that changes both `tab-font` and `menu-font` runs both branches.

Concrete failure scenario:

1. User edits `~/.config/wm2-born-again/config` to change `tab-font` and, in the
   same edit, sets `menu-font` to a pattern with no usable face.
2. `wm2-ctl reload` → `reloadConfigFromDisk` → `applyConfig`.
3. `reloadTabFont` succeeds: the new face is installed and `m_tabWidth` is
   recomputed. `reloadMenuFont` fails; `applyConfig` returns `false` at `:2072`.
4. `m_config` still holds the *old* `tabFont`. No relayout runs — the
   `relayoutFrameForFont()` loop at `:2121-2123` is downstream of the early
   return. Every existing frame keeps the old tab width while its label is now
   drawn with the new face's metrics: labels clipped or overflowing the shaped
   tab, and the shape mask no longer matching the drawn glyphs.
5. `wm2-ctl get tab-font` reports the **old** pattern, which is not what is on
   screen. Worse, the state is sticky: a subsequent `set tab-font <old value>`
   computes `tabFontChanged == false` and never relayouts, so there is no
   client-reachable way to repair it short of a successful reload or a restart.

**Fix:** validate both faces before committing either. `reloadTabFont` /
`reloadMenuFont` should be split into "open a face" (pure) and "install it"
(commit), with both opens done first:

```cpp
// --- Fonts: open BOTH before installing EITHER --------------------------
x11::XftFontPtr newTabFace, newMenuFace;
TabFontRung tabRung = TabFontRung::NoFont;

if (tabFontChanged && !Border::openTabFace(this, next.tabFont, newTabFace, tabRung)) {
    reasonOut = "no usable face for that tab-font pattern";
    return false;
}
if (menuFontChanged && !openMenuFace(next.menuFont, newMenuFace)) {
    reasonOut = "no usable face for that menu-font pattern";
    return false;      // nothing has been swapped; the previous state stands
}

m_config = next;
if (tabFontChanged)  Border::installTabFace(this, std::move(newTabFace), tabRung);
if (menuFontChanged) installMenuFace(std::move(newMenuFace));
```

The same reasoning applies one paragraph up: `Border::reloadColours()` succeeding
and `reloadMenuColours()` failing (`:2047-2048`) leaves the frame palette new and
the menu palette old. The pre-flight at `:2038-2045` makes it improbable rather
than impossible, and the comment at `:2049-2052` says so — split those two the
same way, or the comment is the only thing holding the invariant.

Add a `[wm_config_live]` case using the existing
`WM2_FORCE_TAB_FONT_RELOAD_FAILURE` lever's sibling: reload a file that changes
both fonts with the menu one forced to fail, then assert `get tab-font` equals
what is actually being drawn with.

---

## Warnings

### WR-01: The config writer treats an unreadable target as an absent one and replaces the whole file

**File:** `src/ConfigFileWriter.cpp:199-201`

**Issue:**

```cpp
std::error_code ec;
exists = std::filesystem::exists(path, ec);
if (!exists) return true;          // ec is never inspected
```

`std::filesystem::exists` returns `false` **and sets `ec`** on ELOOP,
ENAMETOOLONG, EACCES on a path component, and EIO. The caller then proceeds with
an empty `lines` vector, emits only the edits plus the section comment, and
renames over the path — silently discarding every line the user had in the file.
Compare `readLines`'s own `is_open()` branch at `:203-204`, which correctly
returns `false` → `ReadFailed`. The failure mode here is silent data loss, which
is exactly what the surgical writer exists to prevent.

**Fix:**

```cpp
std::error_code ec;
exists = std::filesystem::exists(path, ec);
if (ec) {
    errno = ec.value();
    return false;          // ReadFailed -- "could not read X", never "X is absent"
}
if (!exists) return true;
```

### WR-02: No protection against a lost update between two concurrent savers

**File:** `src/ConfigFileWriter.cpp:280-491`

**Issue:** `configFileWrite` is read-modify-write with no lock and no
staleness check. Two `wm2-config` windows (the program is deliberately
`G_APPLICATION_NON_UNIQUE` — `apps/wm2-config/main.cpp:681-683`), or one window
and a hand edit, interleave as: A reads → B reads → A renames → B renames. B's
output was computed from the pre-A file, so A's edits are gone. `rename()` gives
atomicity of the *file*, not of the read-modify-write, and the plan's "the last
one to press Save wins" (`main.cpp:6-8`) understates it: the loser's edits vanish
with no diagnostic.

**Fix:** take an advisory lock on the target for the whole cycle, or `fstat` the
target after the read and compare `st_mtim`/`st_ino` before the rename, failing
with a new `ConfigWriteResult::Stale` the GUI reports as "the file changed on
disk since this window read it".

```cpp
const int lockFd = ::open((path + ".lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
if (lockFd >= 0) ::flock(lockFd, LOCK_EX);     // released by close() below
// ... read, classify, emit, write, rename ...
if (lockFd >= 0) ::close(lockFd);
```

### WR-03: Values with leading or trailing whitespace do not round-trip

**File:** `src/ConfigFileWriter.cpp:113-144` (`editsAreAcceptable`), `src/Config.cpp:125-127`

**Issue:** `editsAreAcceptable` rejects `\n`, `\r` and >256 bytes, but not
leading/trailing whitespace. `Config::applyFile` trims the value
(`src/Config.cpp:127`, `trim(value)`), so a value of `" xterm"` is written as
`new-window-command =  xterm` and read back as `"xterm"`. The live window
manager holds `" xterm"` (the `String` arm of `applyConfigSet` takes values
verbatim — `src/Manager.cpp:2251-2257`), the file says `"xterm"`, and `get`
disagrees with the file the moment anything reloads. A value that is entirely
whitespace becomes the empty string.

**Fix:** refuse it in the writer's own vocabulary, beside the newline check:

```cpp
if (!e.value.empty() &&
    (e.value.front() == ' ' || e.value.front() == '\t' ||
     e.value.back()  == ' ' || e.value.back()  == '\t')) {
    errorOut = "the value for '" + e.key + "' begins or ends with a space, "
               "which the configuration file cannot preserve";
    return false;
}
```

### WR-04: The write-failure message reports a stale errno

**File:** `src/ConfigFileWriter.cpp:463-471`

**Issue:**

```cpp
if (writeOk && ::fsync(fd) != 0) writeOk = false;
const int writeErrno = errno;                    // :465 -- captured BEFORE close
if (::close(fd) != 0 && writeOk) writeOk = false;
if (!writeOk) {
    errorOut = "could not write '" + tmpPath + "': " + std::strerror(writeErrno);
```

When the failure is `close()` (which is where deferred write errors surface on
NFS and on some journalling filesystems), `writeErrno` holds whatever `errno`
was after a *successful* `fsync` — commonly 0, producing `could not write
'...': Success`. The user is told a save failed and given no cause.

**Fix:** capture the errno at each failure site.

```cpp
int failErrno = 0;
if (writeOk && ::fsync(fd) != 0) { writeOk = false; failErrno = errno; }
if (::close(fd) != 0 && writeOk)  { writeOk = false; failErrno = errno; }
// and set failErrno = errno inside the write() loop's error arm too
```

### WR-05: A symlinked config file is silently replaced by a regular file

**File:** `src/ConfigFileWriter.cpp:474`

**Issue:** `rename(tmpPath, path)` replaces the *link*, not its target. A user
who keeps `~/.config/wm2-born-again/config` as a symlink into a dotfiles
repository (a common arrangement) loses the link on the first Save, and every
later edit goes to the real file while the repo copy silently stops being the
source of truth. Not writing through the symlink is the security-correct choice;
the defect is doing it silently.

**Fix:** detect it and say so, rather than resolving it:

```cpp
struct stat lst {};
if (::lstat(path.c_str(), &lst) == 0 && S_ISLNK(lst.st_mode)) {
    errorOut = "'" + path + "' is a symbolic link. Saving would replace the "
               "link with a regular file; edit the file it points at instead.";
    return ConfigWriteResult::WriteFailed;
}
```

### WR-06: accept() failing with EMFILE/ENFILE spins the event loop at 100% CPU

**File:** `src/SocketServer.cpp:416-451`

**Issue:**

```cpp
for (;;) {
    const int fd = ::accept(m_listenFd, nullptr, nullptr);
    if (fd < 0) break;   // EAGAIN on a non-blocking listener: nothing left
```

Every `accept()` error is treated as "nothing left". On `EMFILE`/`ENFILE` the
connection stays pending, `poll()` is level-triggered, and the listener is
readable again immediately — so `nextEvent()`/`modalWait()` spin at full CPU for
as long as the process is out of descriptors, with no diagnostic. On a 512 MB
VPS with a VNC server that is the difference between a degraded desktop and an
unusable one. `ECONNABORTED` is also mishandled (it should continue to the next
pending connection, not abandon the batch).

**Fix:**

```cpp
if (fd < 0) {
    if (errno == EINTR || errno == ECONNABORTED) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    if (errno == EMFILE || errno == ENFILE) {
        // Descriptors are exhausted. Stop polling the listener until the next
        // connection is reaped, rather than re-reading it at full CPU.
        std::fprintf(stderr, "wm2: warning: out of descriptors accepting a "
                             "configuration socket connection (%s)\n",
                     std::strerror(errno));
        m_acceptStalled = true;   // cleared in reap(); appendPollFds() skips POLLIN
        break;
    }
    break;
}
```

### WR-07: The foreign-peer warning becomes floodable once 64 uids are remembered

**File:** `src/SocketServer.cpp:601-618`

**Issue:**

```cpp
if (m_warnedUids.size() < 64) m_warnedUids.push_back(peer);
std::fprintf(stderr, "wm2: warning: refused configuration socket "
                     "connection from uid %lu\n", ...);
```

Once the vector holds 64 entries, a 65th uid is never remembered but the warning
is still printed on **every** attempt. A process cycling uids (or a machine with
many service accounts) reconnects in a loop and fills the session log — which is
the exact outcome T-9-19 and the comment at `:603-605` say the log-once rule
exists to prevent.

**Fix:** make the bound a refusal to warn, not just a refusal to remember:

```cpp
if (m_warnedUids.size() >= 64) {
    if (!m_warnedUidsSaturated) {
        m_warnedUidsSaturated = true;
        std::fprintf(stderr, "wm2: warning: more than 64 distinct uids have been "
                             "refused; further refusals are not logged\n");
        std::fflush(stderr);
    }
    return;
}
m_warnedUids.push_back(peer);
```

### WR-08: The stale-socket probe uses a blocking connect() during startup

**File:** `src/SocketServer.cpp:166-176`

**Issue:** the probe socket is created with `SOCK_STREAM | SOCK_CLOEXEC` — no
`O_NONBLOCK` — and `::connect()` on an `AF_UNIX` stream socket **blocks** when
the peer's listen backlog is full. A same-uid process that binds the socket path,
calls `listen(fd, 1)` and never accepts will hold the window manager inside
`ConfigSocketServer::listen()` indefinitely, before the event loop or the root
window even exist. The window manager never starts and prints nothing.

**Fix:** make the probe non-blocking and bound it.

```cpp
const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
if (fd < 0) return StaleVerdict::Live;

int rc = ::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
int err = errno;
if (rc < 0 && (err == EINPROGRESS || err == EAGAIN)) {
    struct pollfd p { fd, POLLOUT, 0 };
    if (::poll(&p, 1, 250) <= 0) { ::close(fd); return StaleVerdict::Live; }
    socklen_t len = sizeof(err);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
        ::close(fd); return StaleVerdict::Live;
    }
    rc = (err == 0) ? 0 : -1;
}
::close(fd);
```

There is also a TOCTOU between the `lstat` at `:155` and the `unlink` the caller
performs at `src/SocketServer.cpp:241`: the path can be swapped between the two.
Both operations are inside the 0700 directory, so this needs a same-uid attacker
and is a hardening note rather than a boundary break — but the fix is the same
`O_NOFOLLOW`/descriptor discipline CR-03 asks for.

### WR-09: ProtocolClient::send() blocks the GTK main loop, and its EAGAIN arm busy-spins

**File:** `apps/wm2-config/ProtocolClient.cpp:178-199`, `apps/wm2-config/ProtocolClient.cpp:95`

**Issue:** the client socket is created **blocking** (`:95`, no `O_NONBLOCK`) and
`send()` is called from GTK signal handlers via `request()` → `applyLive()`
(`main.cpp:339-349`) and `discard()` (`main.cpp:533-541`, up to 21 sends in a
row). If the window manager stops reading — inside a long modal grab servicing
another client, stopped with SIGSTOP, or wedged — the settings window freezes
with no repaint and no way out. The header's own claim, "Nothing here ever blocks
on a read" (`ProtocolClient.h:20-21`), is silent about writes, and writes are
where this program spends its time.

Second problem in the same function: the `EAGAIN` arm

```cpp
if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    if (Clock::now() >= until) return false;
    continue;                 // no poll, no sleep
}
```

busies the CPU for up to `kHandshakeMs` (5 s) with no wait between attempts. It
is unreachable today only because the descriptor is blocking; the moment anyone
adds `O_NONBLOCK` (which WR-09's own fix does) it becomes a 5-second spin.

**Fix:** make the descriptor non-blocking and give the EAGAIN arm a `poll()`:

```cpp
const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
...
if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    const auto left = until - Clock::now();
    if (left <= Clock::duration::zero()) return false;
    struct pollfd p { m_fd, POLLOUT, 0 };
    const int ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(left).count());
    if (::poll(&p, 1, ms > 0 ? ms : 1) <= 0) return false;
    continue;
}
```

(`connect()` at `:102` has the same blocking exposure at startup, bounded by the
listen backlog; the same non-blocking treatment covers it.)

### WR-10: ProtocolClient::onReadable() drains without a bound when frames contain newlines

**File:** `apps/wm2-config/ProtocolClient.cpp:269-298`

**Issue:** the read loop appends to `m_in` until `EAGAIN`, and only *then*
parses frames. The growth bound

```cpp
if (m_in.size() > kConfigProtocolMaxLine * 2 &&
    m_in.find('\n') == std::string::npos) {
```

requires the absence of a newline. A peer streaming well-formed short lines
faster than the loop exits keeps the GTK main loop inside `onReadable()` with
`m_in` growing without limit — the window freezes and the process grows until
the OOM killer takes it. This is the same "single recv per readable
notification" rule the window manager side states as non-negotiable
(`src/SocketServer.cpp:8-14`, `:455-461`) and the client does not follow.

**Fix:** bound the drain by total bytes per callback, and parse frames as they
complete rather than after the drain:

```cpp
std::size_t drained = 0;
for (;;) {
    if (drained >= kConfigSocketMaxPending) break;   // finish next callback
    ...
    m_in.append(buf, n);
    drained += n;
    if (m_in.size() > kConfigProtocolMaxLine * 2) {   // newline or not
        disconnect(State::Refused, "the window manager sent more than the "
                                   "protocol allows without a complete message");
        return;
    }
}
```

### WR-11: The GLib fd source is left attached to a descriptor ProtocolClient has already closed

**File:** `apps/wm2-config/main.cpp:250-277`, `apps/wm2-config/ProtocolClient.cpp:50-55`, `apps/wm2-config/ProtocolClient.cpp:202-212`

**Issue:** `m_socketSource` is cleared only inside `onSocketReadable`
(`main.cpp:271`). But `ProtocolClient::disconnect()` — which `close()`s `m_fd` —
is also reachable from `request()` (`ProtocolClient.cpp:207`) when `send()`
fails, and that path runs from a GTK button/entry handler, not from the source
callback. Afterwards the GLib source is still watching a closed descriptor
number. GLib polls it, gets `POLLNVAL`, and fires the callback repeatedly until
`onSocketReadable` happens to remove it; and in the interval the fd number may
have been reused by GDK, cairo or fontconfig, so the source is watching an
unrelated descriptor of another subsystem.

Related: there is no reconnect path at all — `connect()` is called once, from the
constructor (`main.cpp:234`). A window manager restarted during a session leaves
the settings window permanently file-only with no retry offered.

**Fix:** own the source in one place, tied to the descriptor's life.

```cpp
void detachSocketSource()
{
    if (m_socketSource != 0) { g_source_remove(m_socketSource); m_socketSource = 0; }
}

// connectToWindowManager() / attachSocketSource():
m_client.setStateHandler([this]() {
    if (!m_client.connected()) detachSocketSource();   // fires on EVERY disconnect
    render();
});
```

`setState()` already runs on every state transition including the one
`disconnect()` performs (`ProtocolClient.cpp:72`), so this covers all callers
without any of them having to know about GLib.

### WR-12: wm2-ctl reports success as failure when a concurrent reload broadcast arrives

**File:** `apps/wm2-ctl/main.cpp:437-443`, `apps/wm2-ctl/main.cpp:297-307`

**Issue:** `wm2-ctl` completes the handshake, so it is in the window manager's
broadcast set (`src/SocketServer.cpp:549-551`). `conn.receive(reply)` returns the
first line that arrives, and `reportReply(reply, expected)` treats anything that
is not `expected` as a refusal:

```cpp
if (reply.type != expected) {
    warn("unexpected reply: " + std::string(configMessageTypeName(reply.type)));
    return kExitRefused;      // exit 1
}
```

Concrete failure: `wm2-ctl set tab-background '#123456'` runs while a
`wm2-config` window presses "Re-read files". The `reloaded` broadcast reaches
`wm2-ctl` before its own `ack`. The set **succeeded**, and `wm2-ctl` exits 1
with "unexpected reply: reloaded" — which the exit-code contract at
`apps/wm2-ctl/main.cpp:21-29` defines as "the window manager refused the
request". A shell script branching on that code takes the wrong branch.

**Fix:** skip unsolicited notices while waiting for the reply the request asked
for:

```cpp
ConfigMessage reply;
for (;;) {
    if (!conn.receive(reply)) { warn("no reply from the window manager"); return kExitNoSocket; }
    // D-08's broadcast shares the `reloaded` type with a reload REPLY. When it
    // is not what this invocation asked for, it belongs to somebody else.
    if (reply.type == ConfigMessageType::Reloaded &&
        expected != ConfigMessageType::Reloaded) continue;
    break;
}
return reportReply(reply, expected);
```

### WR-13: Live apply runs inside modal grabs and mutates state the grab has already cached

**File:** `src/Events.cpp:389` (`modalWait` → `serviceConfigSocket`), `src/Buttons.cpp:314` and `:388` (`entryHeight`, `rowAt`), `src/Client.cpp:1811-1812` (`xoff`, `yoff`)

**Issue:** DISC-06 deliberately services the configuration socket from
`modalWait()`, so a `set` can be applied while the root menu is open or a
move/resize drag is in progress. `applyConfig()` guards exactly one thing — the
manual menu-entry rebuild, behind `m_menuOpen` (`src/Manager.cpp:2147-2154`) —
and nothing else. Two concrete consequences:

- **Menu.** `WindowManager::menu()` computes `entryHeight` once, from
  `m_menuFont` (`src/Buttons.cpp:314`), and uses it for row layout, for
  `rowAt()`'s hit test (`:388`) and for the label baselines (`:409`). A
  `set menu-font` serviced from `modalWait` swaps `m_menuFont` under it
  (`src/Manager.cpp:1975-1978`). The stale `entryHeight` means labels are painted
  at new-face baselines in old-height rows, and the pointer highlights a
  different row from the one it activates. `reloadMenuFont`'s comment
  (`src/Manager.cpp:1980-1988`) correctly refuses to re-lay out an open menu but
  does not prevent the face from changing under it.
- **Move.** `Client::move()` caches `xoff = m_border->xIndent() - e->x`
  (`src/Client.cpp:1811`). A `set frame-thickness` mid-drag changes `FRAME_WIDTH`
  and calls `relayoutFrame()` on every client (`src/Manager.cpp:2101-2105`),
  including the one being dragged. `xIndent()` moves; `xoff` does not; the window
  jumps by the delta on the next motion and commits the wrong position on
  release.

**Fix:** extend the existing deferral idiom rather than adding new guards. The
`m_menuOpen` / `m_appCategoriesStale` pair is already the right shape — generalise
it to a `m_modalDepth` counter incremented by `modalWait()` and consulted by
`applyConfig()`, with the whole application deferred (and the client answered
with `ack`, or with an `error` naming the reason) while a grab is held:

```cpp
// modalWait(), around the body:
++m_modalDepth;  ...  --m_modalDepth;

// applyConfig(), after validation and before m_config = next:
if (m_modalDepth != 0 && applicationTouchesLiveGeometry(next, previous)) {
    reasonOut = "a menu or a drag is in progress; try again in a moment";
    return false;
}
```

At minimum, refuse `menu-font`, `tab-font` and `frame-thickness` while
`m_modalDepth != 0`; those are the three that move geometry a grab has cached.

### WR-14: The button-hold timer over-counts elapsed time now that the poll timeout is socket-clamped

**File:** `src/Border.cpp:1925`, `src/Events.cpp:224-229` (`clampPollTimeoutForSocket`), `src/Events.cpp:367`

**Issue:**

```cpp
if (wait == WindowManager::ModalWait::Timeout) { tdiff += 50; continue; }
```

`tdiff` accrues a hard-coded 50 ms per `Timeout`, on the assumption that
`modalWait(..., 50)` waited 50 ms. Since this phase, `modalWait` clamps its poll
timeout with `clampPollTimeoutForSocket()` (`src/Events.cpp:367`), which returns
`min(base, timeoutHintMs())`. When a connection is within 50 ms of its 10-second
silence deadline the poll returns after a few milliseconds with `r == 0`,
`modalWait` returns `Timeout` (`src/Events.cpp:378`), and `tdiff` gains a full
50 ms for a fraction of that. `tdiff` is what escalates a tab-button hold from
hide to destroy, so the escalation can fire early. The exposure is bounded (one
extra tick per silent connection) but it is a real accuracy regression introduced
by the clamp, and the same pattern will bite harder if any future hint source is
added.

**Fix:** measure the wait instead of assuming it.

```cpp
const auto before = std::chrono::steady_clock::now();
const WindowManager::ModalWait wait = windowManager()->modalWait(..., &event, 50);
...
if (wait == WindowManager::ModalWait::Timeout) {
    tdiff += static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - before).count());
    continue;
}
```

### WR-15: A ';' in a menu entry saves to the file but can never travel over the socket, and silently breaks the GUI's read-back

**File:** `src/Config.cpp:784-800` (`configMenuEntriesValue`), `src/Config.cpp:801-847` (`parseMenuEntriesValue`), `apps/wm2-config/MenuModel.h:77-88` (`MenuEntryDraft::complete`), `src/ConfigFileWriter.cpp:179-190` (`appendMenuEntry`)

**Issue:** the `menu-entries` wire grammar separates records with `;` and has no
escape. The **file** grammar has no such separator — `menu-entry-name = Foo;Bar`
is a perfectly valid line. `MenuEntryDraft::complete()` validates only that the
name is non-empty and the argv non-empty; nothing rejects `;`.

Concrete failure scenario:

1. The user adds a menu entry named `Mail; News`. `commitRows` → `applyLive` →
   `set menu-entries ...`. The window manager's round-trip check
   (`src/Manager.cpp:2186-2205`) refuses it; the status line shows an error the
   user may not connect to the semicolon.
2. The user presses Save anyway. `appendMenuEntry` writes
   `menu-entry-name = Mail; News`, which `Config::applyFile` reads back
   correctly. The entry is now in the window manager's config.
3. From then on, `get menu-entries` returns a value containing that `;`, and
   `adoptEffectiveMenuEntries`'s parse fails:
   `if (!parseMenuEntriesValue(reply.value, entries, reason)) return;`
   (`apps/wm2-config/main.cpp:302-308`) — a **bare return, no status message**.
   The Menu page silently stops tracking what the window manager actually has,
   for the rest of the session and every session after, with no indication why.

**Fix:** refuse the character where the user types it, and make the silent
`return` speak:

```cpp
// MenuModel.h, MenuEntryDraft::complete()
for (const auto& part : {std::cref(name), std::cref(command), std::cref(category)}) {
    if (part.get().find(';') != std::string::npos) {
        reasonOut = "A ';' cannot be used here -- it is what separates entries "
                    "when the list is sent to the window manager.";
        return false;
    }
}
```

```cpp
// main.cpp:306
if (!parseMenuEntriesValue(reply.value, entries, reason)) {
    status("Could not read the menu entries the window manager reported: " + reason);
    return;
}
```

### WR-16: showGeometry's stack buffer is too small for its own format

**File:** `src/Buttons.cpp:763-764`

**Issue:**

```cpp
char string[20];
std::sprintf(string, "%d %d\n", x, y);
```

Two `int`s at their widest produce `"-2147483648 -2147483648\n"` — 24 bytes plus
the terminator, into a 20-byte buffer. The current callers pass screen-clamped
coordinates (`src/Client.cpp:1885-1886`, resize path), so it is latent rather
than live, and it predates this phase. But `showGeometry` is a public method with
no documented bound on its arguments, sitting in a file this phase edits, and
`sprintf` gives it no way to fail safely.

**Fix:**

```cpp
char string[32];
std::snprintf(string, sizeof(string), "%d %d\n", x, y);
```

### WR-17: install-components.sh mishandles an absolute build directory

**File:** `scripts/gates/install-components.sh:39`, `scripts/gates/install-components.sh:84`

**Issue:**

```bash
BUILD_DIR="${1:-build/debug}"
...
STAGE_ROOT="$REPO_ROOT/$BUILD_DIR/install-components"
```

`BUILD_DIR` is used both as a path passed straight to `cmake` (`:63`, `:69`,
`:99`) and as a fragment concatenated after `$REPO_ROOT`. Given
`install-components.sh /tmp/mybuild`, `cmake` operates on `/tmp/mybuild` while
staging goes to `$REPO_ROOT//tmp/mybuild/install-components` — the gate creates
a `tmp/mybuild/` tree inside the checkout, and the `cleanup` trap's guard
(`:89-91`) still matches, so the litter is removed but the directories it made
are not. The usage text advertises only relative paths, but nothing enforces it.

**Fix:**

```bash
BUILD_DIR="${1:-build/debug}"
case "$BUILD_DIR" in
    /*) echo "wm2: install-components: build-dir must be relative to the repository root" >&2
        exit 2 ;;
esac
```

or normalise once with `BUILD_DIR=$(cd "$BUILD_DIR" && pwd)` and use the absolute
form everywhere, including `STAGE_ROOT`.

---

## Info

### IN-01: `Pending::expected` is written and never read

**File:** `apps/wm2-config/ProtocolClient.h:117`, `apps/wm2-config/ProtocolClient.cpp:210`

Dead field. It is the symptom of CR-02 rather than a separate problem, and the
fix for CR-02 puts it to work — but as it stands, a reader sees a field that
implies a correlation check the code does not perform.

### IN-02: The decoder rejects a repeated `type` member but accepts every other repetition

**File:** `include/ConfigProtocol.h:558-563` vs `:564-575`

`if (sawType) return ConfigDecodeResult::Malformed;` guards `type` only.
`{"type":"set","key":"a","key":"b","value":"x"}` decodes with `key == "b"` —
last-wins. DISC-01c treats an unknown member as malformed; a repeated member is
the same class of ambiguity. Track a `seen` bitmask and reject any repetition,
so two decoders (or a decoder and a future reimplementation) cannot disagree
about which value a duplicated member carries.

### IN-03: `adoptEffective` can leave a field dirty with `current == effective`

**File:** `apps/wm2-config/FormState.cpp:113-139`, `apps/wm2-config/FormState.cpp:236-251`

When a dirty field receives an `adoptEffective` whose value equals what the user
typed, `staleUnderEdit` is cleared but `dirty` stays true. `divergentKeys()`
(`current != effective`) then disagrees with `dirty()`, so `save()` writes a line
for a value already in force. Harmless output, but the two predicates now mean
different things and a later reader will assume they agree. Recompute
`field->dirty = (field->current != field->effective);` at the end of the dirty
branch.

### IN-04: `residentKb` silently reports 0 KB if `sysconf` fails

**File:** `tests/support/WmFixture.h:981-991`

`out = resident * (::sysconf(_SC_PAGESIZE) / 1024);` — a `sysconf` failure
returns -1, `-1 / 1024` is 0, and the function returns `true` with `out == 0`.
Every memory-budget assertion built on it then passes vacuously. Since the
comment states this reader exists so the window manager's figure and the GUI's
figure are comparable against one 512 MB budget, a silently-zero reading is
worse than a `false`. Check `sysconf`'s return and fail:

```cpp
const long page = ::sysconf(_SC_PAGESIZE);
if (page <= 0) return false;
out = resident * (page / 1024);
```

### IN-05: The menu-entry dialog does not check the writer's 256-byte field bound

**File:** `apps/wm2-config/MenuModel.h:77-88`, `src/ConfigFileWriter.cpp:146-169`

`MenuEntryDraft::complete()` checks emptiness; `menuEntriesValueFits()` checks the
whole-list wire bound. Neither checks the per-field 256-byte bound
`menuEntriesAreAcceptable()` enforces. A long command is accepted by the dialog,
applied live, and then fails at Save with "the menu entry command is longer than
256 characters" — a late refusal for something the dialog could have said
immediately, in the field where it was typed.

### IN-06: install-components.sh silences a genuine wm2-config build failure

**File:** `scripts/gates/install-components.sh:75-77`

```bash
if cmake --build "$BUILD_DIR" --target wm2-config --parallel >/dev/null 2>&1; then
    GUI_BUILT=true
fi
```

A compile error in `wm2-config` in a GUI-*enabled* tree is indistinguishable from
a GUI-disabled tree. `CONFIG_GUI_EXPECTED` becomes empty, and if the install
component picks up a stale binary from a previous build the gate reports
"config-gui: a GUI-disabled tree installed files for this component" — pointing
the reader at the wrong problem entirely. Read the intent from the cache
(`BUILD_CONFIG_GUI`) and let a build failure in an ON/AUTO-with-GTK tree fail
loudly.

### IN-07: The temp file inherits setuid/setgid/sticky bits from the target

**File:** `src/ConfigFileWriter.cpp:441-446`

`::fchmod(fd, st.st_mode & 07777)` copies all twelve permission bits, including
`S_ISUID`, `S_ISGID` and `S_ISVTX`. A config file should never carry them; if one
somehow does, this propagates it to the replacement rather than dropping it.
`st.st_mode & 0777` is the conservative mask for a data file.

### IN-08: The sockaddr_un length check is spelled twice in wm2-ctl

**File:** `apps/wm2-ctl/main.cpp:155-158` vs `apps/wm2-ctl/main.cpp:420-423`

`Connection::open()` open-codes `path.size() + 1 > sizeof(addr.sun_path)` while
`main()` already called `configSocketPathFits(socketPath)` fifteen lines earlier.
The header explains at length (`include/SocketServer.h:115-121`) why this
predicate has exactly one home; the duplicate is a second copy that can drift
from it. Call `configSocketPathFits(path)` in `open()` too, or drop the check
there and let the single call site in `main()` own it.

---

_Reviewed: 2026-09-06T16:43:45Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
