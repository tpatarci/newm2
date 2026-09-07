---
phase: 09-config-gui-ipc
plan: 07
subsystem: ui
tags: [gtk3, glib, unix-socket, ipc, catch2, xvfb, xtest, root-menu, tdd]

requires:
  - phase: 09-config-gui-ipc
    provides: "09-06's window, notebook, FormState, ProtocolClient, ConnectionState.h and the display-free/skip-with-a-reason shape of tests/test_wm2_config_smoke.cpp -- every one of which this plan extended rather than re-invented"
  - phase: 09-config-gui-ipc
    provides: "09-05's applyConfig() returning bool, its live menu-entry rebuild, AppCache::loadAutoDiscovered(), and the [wm_config_live] suite that owns the observable-behaviour proof for eight of the nine Behaviour settings"
  - phase: 09-config-gui-ipc
    provides: "09-04's configKeySpecs() / configKeySpecFor() -- the delay controls' ranges and every Behaviour tooltip are read from it at runtime"
  - phase: 09-config-gui-ipc
    provides: "09-02's frozen version-1 codec and the surgical writer's menuEntries / rewriteMenuEntries parameters, which the Menu page's save uses instead of an edit"
  - phase: 07-root-menu-application-discovery
    provides: "AppEntry, AppCache::mergeEntries() and D-07's Custom default -- the row shape the Menu page edits and the merge the category answer is computed from"
  - phase: 08.5-v1.0-closeout
    provides: "tests/support/WmFixture.h and tests/support/XTestDriver.h -- the Xvfb fixture and the pointer synthesis the tracer case drives focus with"
provides:
  - "`apps/wm2-config/BehaviourPage.{h,cpp}` -- the four focus booleans, the three delays, the new-window command and the shell flag, every tooltip assembled at runtime from the option table's own summary"
  - "`apps/wm2-config/MenuPage.{h,cpp}` -- a name/command/category row list with Add, Edit, Remove and an entry dialog whose category control is a dropdown-plus-free-text"
  - "`apps/wm2-config/MenuModel.h` -- GTK-free: MenuEntryDraft (the dialog's contents and its argument-vector readout), menuCategoriesFrom() (the file-only fallback), kMenuCategoryFileOnlyReason, menuEntriesValueFits() (T-9-44's bound, measured on the ENCODED line)"
  - "`configTokeniseCommand()` in include/Config.h -- ONE whitespace split with two callers: the config parser's menu-entry-command arm and the settings window's dialog"
  - "`menu-categories` -- a read-only key on the existing `get` verb answering the categories the NEXT root menu will show, in the menu's own order. No twelfth message type"
  - "`FormState`'s menu half: the entry list as current/effective/belowUser with its own dirtiness, `adoptEffectiveMenuEntries()`, `requestResetAll()` and `revertAndCollectRestores()`"
  - "`FormField::staleUnderEdit` -- D-08's mark, which is the DISAGREEMENT and not the edit"
  - "The close-with-unsaved-changes prompt (Save / Discard / Cancel) on the window's delete event, with Discard sending the saved values back to the running desktop"
  - "Per-page 'Reset this page' on all three pages, built from the per-setting reset"
  - "ctest label `wm2_config_smoke`: 50 cases (was 22). Suite 526 (was 498)"
  - "Five screenshots: the three pages, the entry dialog and the close prompt"
affects: [09-08-install-components, 09-09-release, 10-native-x11-configuration-tool]

actuals:
  tokens: 42000     # chars/4 over the realized diff (169,445 chars, 52b22db..c134ea9, PNGs excluded)
  tasks: 3
  commits: 7          # MEASURED: git rev-list --count 52b22db..HEAD -- 6 production commits (3 RED, 3 GREEN) plus this SUMMARY
  plan_head_before: 52b22db7276a99591ed7dbcbbd45d99c015171d1

tech-stack:
  added:
    - "libXtst on tests/test_wm2_config_smoke.cpp only, for the one tracer case that drives real pointer input. The shipped wm2-born-again target still links none of it."
  patterns:
    - "A test derives a page's key list FROM THE PAGE'S OWN SOURCE, then iterates it: a setting added to a page gets its live case for free, and a setting added with no live path cannot slip past by not being on a list in the test file"
    - "A page's wording is ASSEMBLED at runtime from configKeySpecFor(key)->summary, so the window, --help and the release notes are one sentence with three readers rather than three strings that drift"
    - "A read-only key on an existing verb, rather than a new message type, when a client needs a DERIVED view the window manager alone can compute. Read-only by construction: the key is absent from configKeySpecs(), so the set arm refuses it with no special case"
    - "The dialog's contents live in a GTK-free struct, which is what makes 'a reload cannot disturb an open dialog' a fact a display-free case can hold both halves of at once rather than a flag to trust"
    - "A source-level guard strips `//` comments first. The function's own comment explaining that it touches no dialog is itself a mention of the token the guard forbids -- the D-33 lesson, met a second time"

key-files:
  created:
    - apps/wm2-config/BehaviourPage.h
    - apps/wm2-config/BehaviourPage.cpp
    - apps/wm2-config/MenuPage.h
    - apps/wm2-config/MenuPage.cpp
    - apps/wm2-config/MenuModel.h
    - .planning/phases/09-config-gui-ipc/evidence/wm2-config/appearance-page.png
    - .planning/phases/09-config-gui-ipc/evidence/wm2-config/behaviour-page.png
    - .planning/phases/09-config-gui-ipc/evidence/wm2-config/menu-page.png
    - .planning/phases/09-config-gui-ipc/evidence/wm2-config/menu-entry-dialog.png
    - .planning/phases/09-config-gui-ipc/evidence/wm2-config/close-prompt.png
  modified:
    - apps/wm2-config/main.cpp
    - apps/wm2-config/FormState.h
    - apps/wm2-config/FormState.cpp
    - apps/wm2-config/AppearancePage.h
    - apps/wm2-config/AppearancePage.cpp
    - include/Config.h
    - include/Manager.h
    - src/Config.cpp
    - src/Manager.cpp
    - tests/test_wm2_config_smoke.cpp
    - CMakeLists.txt

key-decisions:
  - "The window manager answers a new READ-ONLY key, `menu-categories`, on the existing `get` verb. D-12 asks the dropdown for 'the categories the WM currently shows' and there was no route to that: the GUI would have had to re-run the .desktop and /usr/bin scan, which is a second implementation of discovery and therefore a second answer. Eleven message types are untouched -- this is one key added to a verb's vocabulary, exactly what 09-05 did for `menu-entries` -- and it is read-only by construction because the key is not in configKeySpecs(), so applyConfigSet() refuses it as an unknown setting with no special case. A case asserts that refusal."
  - "The command tokeniser was HOISTED rather than copied. The plan said the dialog must tokenise 'exactly as the config parser does'; the only way that is true is one function, so Config::applyKeyValue()'s menu-entry-command arm now calls configTokeniseCommand() and so does the dialog. A case compares the function against a vector the parser produced from a real file."
  - "apps/wm2-config/MenuModel.h exists although the plan's artefact table did not name it. Three things the plan requires -- the argument-vector display (T-9-40), the file-only category fallback and its stated reason, and the line-bound refusal (T-9-44) -- are all asserted by display-free cases, and none of them can live in a header that includes GTK. It is the same reasoning that put D-03's banner sentence in ConnectionState.h in 09-06."
  - "`staleUnderEdit` marks the DISAGREEMENT, not the edit. A reload that happens to bring exactly what the user typed is not something they need told about, and marking it would make the mark mean 'edited' -- which `dirty` already means, and which would light up every control on the page after any reload. A mutation case asserts the agreeing case is NOT marked."
  - "The bucketing rule the root menu orders its categories by (alphabetical, Custom last) now has one implementation with two callers, rather than a second copy inside the new answer. A dropdown offering the categories in an order the menu does not use is exactly the quiet disagreement D-12 asks the window manager to be the source of truth about."
  - "Save asks the MODEL whether anything changed rather than asking the edit list. The menu-entry block is dirty without producing a single ConfigEdit -- the writer rewrites it as a block -- so `edits.empty()` made a Menu-page-only change save nothing at all. The close prompt is keyed on the same predicate, or a user could close over unsaved menu rows without being asked."
  - "The delay controls commit on `value-changed`. A spin button has no drag: every change it emits is one completed adjustment, so that signal IS D-05's release for this control. Typing '50' therefore sends 5 and then 50; both are in range, both are cheap to apply, and the window manager validates each."
  - "The screenshot of the close prompt is reached by HOLDING THE TAB BUTTON past destroy-window-delay -- the window manager's own close path, which is what actually sends WM_DELETE_WINDOW. `xdotool windowclose` is XDestroyWindow and tore the window out from under the toolkit; that crash is a property of the tool, not of this program, and the capture script no longer uses it."

patterns-established:
  - "Key list derived from source, then iterated: `keysDeclaredIn(sourceOf(page))` drives the live cases, and a separate case asserts the two pages PARTITION every settable key -- so a key that no page can edit is a failure rather than an omission nobody notices"
  - "Wording by assembly, not by copy: label (a short human phrase) + tooltip (configKeySpecFor(key)->summary, extended). The agreement with --help is structural rather than a pair of strings somebody must keep in step"
  - "A mutation case beside every mark: 'the edit is kept and marked' is paired with 'a reload that agrees is NOT marked', so the first cannot pass against a form that marks every edited key forever"
  - "Comment-stripped source guards (the D-33 lesson, second occurrence)"

requirements-completed: [CGUI-03, CGUI-04]

coverage:
  - id: D1
    description: "The Behaviour page carries the nine settings D-09 assigns it and nothing D-09 assigns elsewhere, and the two pages between them account for every settable key"
    requirement: CGUI-03
    verification:
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#the Behaviour page carries what D-09 assigns to it and nothing else"
        status: pass
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#the Appearance and Behaviour pages partition the settable keys between them"
        status: pass
    human_judgment: false
  - id: D2
    description: "Click-to-focus committed through the page's own model changes how the running desktop gives focus, on real synthesised pointer input"
    requirement: CGUI-04
    verification:
      - kind: e2e
        ref: "tests/test_wm2_config_smoke.cpp#click-to-focus committed through the page's model changes how the desktop gives focus"
        status: pass
    human_judgment: false
  - id: D3
    description: "Every one of the nine Behaviour settings, committed through the page's model and the GUI's own socket client, reaches the running window manager"
    requirement: CGUI-04
    verification:
      - kind: integration
        ref: "tests/test_wm2_config_smoke.cpp#every Behaviour setting committed through the page's model reaches the running window manager"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting raise-on-focus false focuses a window without restacking it"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting auto-raise false stops the pointer consulting focus at all"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting focus-stealing-prevention false grants focus to a window that asked not to have it"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting auto-raise-delay changes how long the pointer must rest"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting pointer-stopped-delay changes how long stillness must last"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting destroy-window-delay changes what a held tab button does"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting new-window-command and exec-using-shell changes what the menu's New entry runs"
        status: pass
    human_judgment: true
    rationale: "The 09-07 case proves the GUI's own two halves carry each of the nine to the window manager and that it reports the new value. That adopting the value changes an OBSERVABLE behaviour is proven per setting by the [wm_config_live] cases named above, which drive wm2-ctl rather than the GUI's client. Only click-to-focus (D2) has both halves in one case. The join between the two -- 'the value that arrived is the value whose behaviour changed' -- is an inference, not an assertion, for the other eight."
  - id: D4
    description: "Every Behaviour control's wording is the option table's own summary, extended in a tooltip and never contradicted; the shell flag's label states the shell-evaluation consequence"
    requirement: CGUI-03
    verification:
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#every Behaviour control's wording is built from the option table's own summary"
        status: pass
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#the shell flag's label states that the command is shell-evaluated"
        status: pass
      - kind: automated_ui
        ref: ".planning/phases/09-config-gui-ipc/evidence/wm2-config/behaviour-page.png"
        status: pass
    human_judgment: true
    rationale: "That a non-programmer would understand each label is a judgment no test makes. The structural half -- the tooltip cannot contradict --help because it IS --help's sentence -- is asserted; the readability is the operator's screenshot review."
  - id: D5
    description: "The delay controls are clamped to the parser's own range, and the clamp is a convenience rather than the validation -- the window manager refuses an out-of-range value regardless"
    requirement: CGUI-03
    verification:
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#the delay controls are built from the parser's own bounds, not from a third copy"
        status: pass
      - kind: integration
        ref: "tests/test_wm2_config_smoke.cpp#a delay outside the parser's range is refused, so the control's clamp is a convenience and not the validation"
        status: pass
    human_judgment: false
  - id: D6
    description: "A menu row added through the page's model over a real socket appears in the next root menu"
    requirement: CGUI-03
    verification:
      - kind: e2e
        ref: "tests/test_wm2_config_smoke.cpp#a row added through the page's model appears in the next root menu"
        status: pass
    human_judgment: false
  - id: D7
    description: "The entry dialog's category control is a dropdown of what the window manager reports plus free text, with a stated file-only fallback; the key is read-only"
    requirement: CGUI-03
    verification:
      - kind: integration
        ref: "tests/test_wm2_config_smoke.cpp#the category list offered by the page is the one the window manager reports"
        status: pass
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#the file-only category list is the file's own categories plus the Custom default"
        status: pass
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#the Menu page asks the window manager for its categories rather than listing any"
        status: pass
    human_judgment: false
  - id: D8
    description: "A shell metacharacter typed into a menu command stays one literal argument, and the dialog shows the argument vector so the user can see that (T-9-40)"
    verification:
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#a shell metacharacter in a command is one literal argument, and the dialog shows it as one"
        status: pass
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#the Menu page tokenises a command exactly as the configuration parser does"
        status: pass
      - kind: automated_ui
        ref: ".planning/phases/09-config-gui-ipc/evidence/wm2-config/menu-entry-dialog.png"
        status: pass
    human_judgment: false
  - id: D9
    description: "An entry list too long for one protocol line is refused before it is sent, with a refusal naming the limit (T-9-44)"
    verification:
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#a menu-entry list too long for one protocol line is refused before it is sent, and the refusal names the limit"
        status: pass
    human_judgment: false
  - id: D10
    description: "Closing with unsaved changes asks Save / Discard / Cancel, and Discard returns the running window manager to the saved file so the desktop and the file agree afterwards (D-07)"
    verification:
      - kind: integration
        ref: "tests/test_wm2_config_smoke.cpp#Discard returns the running window manager to the saved file's values"
        status: pass
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#Discard in file-only mode puts the form back and sends nothing"
        status: pass
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#closing is silent with nothing unsaved and asks when there is something"
        status: pass
      - kind: automated_ui
        ref: ".planning/phases/09-config-gui-ipc/evidence/wm2-config/close-prompt.png"
        status: pass
    human_judgment: true
    rationale: "The Discard SEQUENCE is asserted end to end against a real window manager through the same revertAndCollectRestores() the window calls. What is not asserted by a test is the button-to-sequence edge -- that pressing Discard in the dialog runs it -- because driving a modal GTK dialog from another process is testing the toolkit. The screenshot is that edge's evidence, and whether the wording makes Discard's consequence unambiguous is the operator's call."
  - id: D11
    description: "A reload notice re-reads effective values, keeps every unsaved edit, marks each one that now differs, and shows a one-line notice (D-08)"
    verification:
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#a reload notice keeps an unsaved edit and marks it rather than replacing it"
        status: pass
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#an unsaved edit that a reload made agree with the file is no longer marked"
        status: pass
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#the window shows the reload notice where the banner is, not in a second status region"
        status: pass
    human_judgment: false
  - id: D12
    description: "A reload notice arriving with an entry dialog open leaves the dialog and its contents intact"
    verification:
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#a reload notice arriving with an entry dialog open leaves the dialog's contents alone"
        status: pass
    human_judgment: true
    rationale: "Asserted STRUCTURALLY: the case holds a MenuEntryDraft across the adoption and shows it is untouched, and a comment-stripped source guard shows the page's refresh path names no dialog and no draft. What is NOT asserted is the live GUI arm -- a real reload arriving while gtk_dialog_run() is spinning -- because reaching that state from outside the process means synthesising input into a toolkit. Recorded in .planning/WINDOWS.md."
  - id: D13
    description: "Reset all on a page marks every setting on it for removal, Save then removes those lines and writes no defaults, and Revert before Save undoes the marking (D-13)"
    requirement: CGUI-03
    verification:
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#Reset all on a page removes that page's keys from the user file and writes no defaults"
        status: pass
      - kind: unit
        ref: "tests/test_wm2_config_smoke.cpp#Revert before Save undoes a Reset all"
        status: pass
    human_judgment: false
  - id: D14
    description: "The whole tree still builds and passes in both gated configurations, and the window manager links no GTK and no GLib"
    verification:
      - kind: manual_procedural
        ref: "bash scripts/gates/build-all.sh debug  ->  526/526, 0 warnings"
        status: pass
      - kind: manual_procedural
        ref: "bash scripts/gates/build-all.sh asan  ->  green, no sanitizer findings"
        status: pass
      - kind: manual_procedural
        ref: "ldd build/debug/wm2-born-again | grep -c -e libgtk -e libglib -e libgobject  ->  0 (and 0 for build/asan; wm2-ctl links no GTK and no X11)"
        status: pass
    human_judgment: false

duration: 105 min
completed: 2026-09-06
status: complete
---

# Phase 9 Plan 7: The Behaviour and Menu Pages, and the Three Divergence Moments Summary

**The settings window is finished: nine behaviour settings whose commits change the running desktop, a menu-entry list whose rows reach the next root menu, and the three moments — closing, reloading, resetting — where a settings tool usually lets the file and the desktop drift apart quietly.**

## Performance

- **Duration:** 105 min
- **Started:** 2026-09-06T13:35:00Z
- **Completed:** 2026-09-06T15:20:00Z
- **Tasks:** 3
- **Files modified:** 21 (10 created — 5 of them screenshots — and 11 modified)

## Accomplishments

- **The Behaviour page carries all nine settings D-09 assigns it, and a tracer case proves the chain end to end.** Click-to-focus, committed through the GUI's own `FormState` and `ProtocolClient`, stops the desktop giving focus to real synthesised pointer input — and a click on the same spot still focuses, so what changed is the route rather than the window manager's ability to focus anything. The case was mutation-checked: committing a different key leaves the pointer focusing and it fails.
- **No page's wording can drift from `--help` any more.** Every Behaviour tooltip is assembled at runtime from `configKeySpecFor(key)->summary`, the sentence the option table already carries. The label above it is a short human phrase that extends its summary. There is one sentence with three readers rather than three strings somebody must keep in step.
- **The Menu page is a list, three buttons and a dialog, and the dialog is honest about what a command becomes.** A typed `/usr/bin/less /var/log/syslog;date` is displayed as `[/usr/bin/less] [/var/log/syslog;date]` — the semicolon visibly inside one argument. That display is the argument vector `configTokeniseCommand()` produces, and that function is now what the config file's parser calls too, so the window and the file cannot produce two different vectors from one line.
- **The category dropdown is the window manager's answer, not a second guess.** A new read-only `menu-categories` key on the existing `get` verb reports what the next root menu will show, in the menu's own order. A case adds a row in a unique category and asserts the answer gains it; another asserts a `set` of the key is refused.
- **Closing the window can no longer leave the desktop matching no file.** The delete event asks Save / Discard / Cancel whenever a save would write something, and Discard actively sends the saved values back — asserted against a real window manager, and reached in the evidence through the window manager's own close path.
- **A reload notice keeps what you were typing and says so.** The mark is the disagreement rather than the edit: a reload that happens to bring exactly what the user typed marks nothing, which a mutation case asserts.
- **The suite went 476 → 498 → 526**, `[wm2_config_smoke]` 22 → 50, both gated trees green, zero compiler and linker warnings, and the window manager still links no GTK and no GLib.

## Task Commits

1. **Task 1 (tracer, tdd): the Behaviour page** — `ae49e29` (test, RED: 7 of 8 failing) → `a7bb4bb` (feat, GREEN)
2. **Task 2 (tdd): the Menu page** — `b16e431` (test, RED) → `15beeab` (feat, GREEN)
3. **Task 3 (tdd): the three divergence moments** — `ad3608f` (test, RED) → `c134ea9` (feat, GREEN, with the five screenshots)

### RED evidence

- **Task 1** is the strongest of the three: the build succeeded and **7 of the 8 new cases failed** on a run, each naming `apps/wm2-config/BehaviourPage.cpp` as absent. The eighth (the out-of-range refusal) is an assertion about the window manager's own validation and does not depend on the page, which is stated in the case.
- **Tasks 2 and 3** are **compile-level RED**: the test target failed to build, naming `apps/wm2-config/MenuModel.h`, then `revertAndCollectRestores()` and `FormField::staleUnderEdit`. That is a weaker RED than a failing run, because it does not demonstrate that each assertion discriminates. Recorded here rather than dressed up, and partly offset by the two mutation checks described below.

### Mutation checks actually performed

- **The tracer.** Changing the committed key from `click-to-focus` to `raise-on-focus` — so the form still accepts, the window manager still acks, and only the BEHAVIOUR is unchanged — makes the case fail with `active after pointer entry: 8388632 second: 8388632`. It is not a value echo.
- **The reload mark.** `an unsaved edit that a reload made agree with the file is no longer marked` is the standing mutation case for `a reload notice keeps an unsaved edit and marks it`: without it, a form that marked every edited key forever would pass the first case.

## Files Created/Modified

- `apps/wm2-config/BehaviourPage.{h,cpp}` — nine controls, per-setting reset, per-page reset, D-08's mark; tooltips assembled from the option table.
- `apps/wm2-config/MenuPage.{h,cpp}` — the row list, Add/Edit/Remove, the entry dialog, the category dropdown, T-9-44's refusal before the model moves.
- `apps/wm2-config/MenuModel.h` — GTK-free: `MenuEntryDraft`, `menuCategoriesFrom()`, `menuCategoriesFromValue()`, `kMenuCategoryFileOnlyReason`, `menuEntriesValueFits()`.
- `apps/wm2-config/FormState.{h,cpp}` — the menu half, `staleUnderEdit`, `requestResetAll()`, `revertAndCollectRestores()`.
- `apps/wm2-config/AppearancePage.{h,cpp}` — the same mark and the same per-page reset, so the three pages behave identically.
- `apps/wm2-config/main.cpp` — the close prompt, the notice region, the menu-entry and menu-category reads, `refreshPages()`, and the save guard fix.
- `include/Config.h`, `src/Config.cpp` — `configTokeniseCommand()`, `kMenuCategoriesKey`.
- `include/Manager.h`, `src/Manager.cpp` — `menuCategoriesValue()`, and the category ordering rule hoisted to one implementation with two callers.
- `tests/test_wm2_config_smoke.cpp` — 28 new cases and the ported fixture helpers.
- `CMakeLists.txt` — the two new sources on the GUI target, `PkgConfig::XTST` on the smoke test target.
- Five screenshots under `.planning/phases/09-config-gui-ipc/evidence/wm2-config/`.

## Decisions Made

See `key-decisions` in the frontmatter. The two worth reading first are the read-only `menu-categories` key (why one key on an existing verb rather than a twelfth message type, and why the GUI must not compute the answer itself) and `staleUnderEdit` marking the disagreement rather than the edit.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocker] D-12's category dropdown had no route to its data**

- **Found during:** Task 2
- **Issue:** The plan says "Ask the window manager for the current category list when connected" and "Do not reimplement the discovery scan in the GUI". No message, key or property carried that list. The protocol's eleven message types are frozen, and D-14 caps the status reply at seven fields.
- **Fix:** A read-only key, `menu-categories`, answered by the existing `get` verb from `AppCache::mergeEntries(m_autoApps, m_config.manualMenuEntries)` bucketed by the same ordering rule the root menu uses. No twelfth message type; the addition is to the key vocabulary of a verb, which is exactly what 09-05 did for `menu-entries`. Read-only by construction: the key is absent from `configKeySpecs()`, so `applyConfigSet()` refuses it as an unknown setting with no special case, and a case asserts that.
- **Files modified:** `include/Config.h`, `include/Manager.h`, `src/Manager.cpp` — none of them in the plan's `files_modified`.
- **Verification:** `the category list offered by the page is the one the window manager reports` (adds a row in a unique category, asserts the answer gains it, asserts a `set` is refused).
- **Committed in:** `15beeab`

**2. [Rule 3 — Blocker] "Tokenise exactly as the config parser does" is only true of one function**

- **Found during:** Task 2
- **Issue:** The parser's whitespace split lived inline in `Config::applyKeyValue()`. A copy in the dialog would agree with it until one of them changed.
- **Fix:** Hoisted to `configTokeniseCommand()` in `include/Config.h`; the parser now calls it and so does `MenuEntryDraft::argv()`.
- **Files modified:** `include/Config.h`, `src/Config.cpp` — not in the plan's `files_modified`.
- **Verification:** `the Menu page tokenises a command exactly as the configuration parser does` compares the function against a vector the parser produced from a real file on disk.
- **Committed in:** `15beeab`

**3. [Rule 1 — Bug] Save did nothing when only the Menu page had changed**

- **Found during:** Task 2
- **Issue:** `ConfigWindow::save()` returned early on `edits.empty()`. The menu-entry block is dirty without producing a single `ConfigEdit`, because the writer rewrites it as a block through its own parameters — so a user who added a row and pressed Save was told "Nothing has changed, so nothing was written".
- **Fix:** The guard asks `m_form.dirty()`. The close prompt uses the same predicate, or a user could have closed over unsaved menu rows without being asked.
- **Files modified:** `apps/wm2-config/main.cpp`
- **Verification:** `the Menu page's rows survive a save and come back as the same list`, and `closing is silent with nothing unsaved and asks when there is something` (which asserts a Menu-only change is dirty AND produces no edits).
- **Committed in:** `15beeab`

**4. [Rule 2 — Missing critical] `apps/wm2-config/MenuModel.h`, a file the plan's artefact table did not name**

- **Found during:** Task 2
- **Issue:** Three things the plan requires proving — the argument-vector display (T-9-40), the file-only category fallback with its stated reason, and the line-bound refusal (T-9-44) — are all asserted by display-free cases, and a header that includes GTK cannot be linked into the display-free test binary. A fourth thing became possible once it existed: holding the dialog's contents across a reload, which is D-08's acceptance criterion.
- **Fix:** A GTK-free header beside `FormState.h`, on the same reasoning that put D-03's banner sentence in `ConnectionState.h` in 09-06.
- **Files modified:** new file.
- **Verification:** five display-free cases read it.
- **Committed in:** `15beeab`

**5. [Rule 2 — Missing critical] Per-page Reset all reached the Appearance page too**

- **Found during:** Task 3
- **Issue:** D-13 says "Reset to defaults exists per setting and per page". The plan's Task 3 says to implement it "per page", and its `files_modified` names neither `AppearancePage.cpp` nor `MenuPage.cpp`. Leaving the Appearance page without one would have made the three pages behave differently for the same command.
- **Fix:** `FormState::requestResetAll()` is the one code path; all three pages carry the button. The Appearance page also gained D-08's mark for the same reason.
- **Files modified:** `apps/wm2-config/AppearancePage.{h,cpp}`
- **Verification:** `Reset all on a page removes that page's keys from the user file and writes no defaults`, driven over the Behaviour page's key list; `Revert before Save undoes a Reset all`.
- **Committed in:** `c134ea9`

### Narrowings, stated plainly

**6. Eight of the nine Behaviour settings get value-adoption here, not a new observable-behaviour case**

- **Task 1's acceptance** asks for "a case per boolean asserting an observable window manager behaviour change" and "a case per delay asserting the timing changed".
- **What was built:** one full tracer (click-to-focus, real pointer input, mutation-checked), plus one case that drives all nine through the GUI's own `FormState` and `ProtocolClient` and asserts the window manager reports each new value, plus the out-of-range refusal case for the delays.
- **Why:** 09-05's `[wm_config_live]` suite already owns one XTEST case per setting proving exactly the observable change asked for here — they are named per setting in `coverage.D3`. Copying eight of them into this file would have been a second copy of a suite rather than a second proof, on a machine the standing rules describe as memory-pressured.
- **What is therefore NOT asserted:** the join — "the value that arrived through the GUI's client is the value whose behaviour changed" — is an inference for those eight rather than an assertion. `coverage.D3` says so and is marked `human_judgment: true`. Recorded in `.planning/WINDOWS.md`.

**7. The reload-with-a-dialog-open case is structural, not a live GUI drive**

- **What was built:** a display-free case that holds a `MenuEntryDraft` across `adoptEffectiveMenuEntries()` and shows it untouched, plus a comment-stripped source guard that `MenuPage::refreshFromForm()` names no dialog and no draft.
- **Why:** reaching the state from outside the process means synthesising input into a modal GTK dialog, which tests the toolkit.
- **Recorded in** `.planning/WINDOWS.md`.

**8. The dialog states the name-override rule unconditionally rather than only when the name matches**

- Task 2's behaviour list says "the dialog says so when the name matches". Detecting a match needs the list of discovered application NAMES, and no key exposes it; adding a second read-only key to publish the user's installed-application list over the socket was a wider surface than a note is worth.
- **What was built:** the page's own blurb and the Name field's tooltip state the rule unconditionally — "An entry whose name matches one that was found replaces it" — which is always true and needs no second list.
- Not one of Task 2's acceptance criteria. Recorded in `.planning/WINDOWS.md` as a deviation.

---

**Total deviations:** 5 auto-fixed (3 × Rule 3 blockers, 2 × Rule 2 missing-critical) and 3 recorded narrowings.
**Impact on plan:** The three blockers were each the only way to make a sentence the plan wrote actually true (one function, one answer, one save). No scope creep: the new protocol surface is one read-only key on an existing verb, and it is refused on `set` by construction. The three narrowings are all coverage narrowings rather than functionality narrowings — every behaviour the plan describes is implemented; what is thinner than the acceptance text asked for is the proof, and each one says exactly how.

## Issues Encountered

**A commit message with backticks in it captured the environment.** The Task 2 commit was written with `git commit -m` and its body contained `` `get` `` and `` `set` ``, which the shell executed as command substitution — the resulting message carried a dump of the environment, including an API key. Caught immediately on reading the message back, amended within the same minute, and the branch's reflog expired so no reachable commit and no reflog entry carries it (`git log --all | grep -c "sk-ant-"` → 0). Nothing was pushed; this worktree has no remote-tracking work. Every commit message since is written with `-F` from a quoted heredoc. Worth recording because the failure mode is silent: `git commit -m` with a backtick anywhere in the body will do this again.

**`xdotool windowclose` crashed the settings window while capturing evidence.** It is `XDestroyWindow`, not a `WM_DELETE_WINDOW` client message, so it tears the window out from under GTK; the toolkit then died on a `BadWindow`. The capture now closes the window the way a user does — holding the tab button past `destroy-window-delay`, which is the window manager's own close path and what actually sends `WM_DELETE_WINDOW`. The crash was a property of the capture tool, not of `wm2-config`; the same route through the real close path produces the prompt correctly, which the screenshot shows.

**`docs/RELEASE-NOTES.md` is untouched.** It does not yet describe `wm2-config` at all — 09-06 did not add it either — and the plan's `files_modified` names no documentation. Phase 9's release plan (09-09) owns that text. Flagged here so it is not mistaken for parity that was checked and found adequate.

## Known Stubs

None. Every control on all three pages is wired to the model, and the model is wired to the file and to the socket.

## Next Phase Readiness

- **09-08 (install components)** can proceed: the GUI's source list in `CMakeLists.txt` is final for this phase, and `BUILD_CONFIG_GUI` is unchanged.
- **09-09 (release)** must write the settings window into `docs/RELEASE-NOTES.md`, including the new read-only `menu-categories` key, which is now part of what a third-party client may rely on from v1.0 under D-8.5-01.
- **Carried forward, unclosed:** two `wm2-config` processes running at once can each Save over the other (09-06's flagged CGUI-01 assumption). Still a v1.1 candidate, still cheap to close with an advisory lock on the user file at Save.
- **Concern for the operator's screenshot review:** the Appearance page's "Reset this page" button sits below the fold at the default window size, because that page is taller than the window. The Behaviour and Menu pages show theirs without scrolling.

---
*Phase: 09-config-gui-ipc*
*Completed: 2026-09-06*

## Self-Check: PASSED

Every file named in `key-files.created` exists on disk (`[ -f ]`, 10/10, including all five
screenshots) and every commit hash named in "Task Commits" is reachable from this branch
(`git log --oneline --all`, 6/6). The plan-level verification was re-run in full: `bash
scripts/gates/build-all.sh debug` is green at 526/526 with zero warnings, `bash
scripts/gates/build-all.sh asan` is green with no sanitizer findings, and the pre-existing
`[wm_menulabel]` and `[wm_config_runtime]` labels were run separately and are green. `ls
.planning/phases/09-config-gui-ipc/evidence/wm2-config/*.png | wc -l` reports 7 (the five this
plan captured plus 09-06's two), against the plan's threshold of 5.
