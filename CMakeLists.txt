cmake_minimum_required(VERSION 3.20)
# D-33: 3.20 is the real floor. The tags-as-labels discovery option is
# implemented in Catch2's CatchAddTests.cmake with string(JSON ...), which needs
# CMake >= 3.19, and every ctest gate in Phase 8 carries --no-tests=error, which
# needs ctest >= 3.20. Declaring an older floor would make a 3.16-3.18 builder
# fail at configure time with an opaque string(JSON) error instead of a named
# cause -- exactly the failure mode scripts/preflight.sh exists to eliminate.
project(wm2-born-again VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- X11 dependencies via pkg-config (BLD-01, BLD-05) ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(XEXT REQUIRED IMPORTED_TARGET xext)
pkg_check_modules(XFT REQUIRED IMPORTED_TARGET xft)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
# D-26 (XDIS-01/XDIS-02): libxrandr is a HARD BUILD dependency, deliberately not
# an optional one. It is part of the base X client stack on every target this
# project supports, so conditional compilation would buy nothing and would fork
# the geometry path in two. What is NOT hard is the RUNTIME availability of the
# extension: XRRQueryExtension is checked at startup and a server without RANDR
# gets a warning plus the root-ConfigureNotify fallback, never a fatal error.
# Build-time required, run-time optional -- the two are independent.
pkg_check_modules(XRANDR REQUIRED IMPORTED_TARGET xrandr)

# XDIS-04: libxrender follows exactly the same build-time-required /
# run-time-optional split as libxrandr above. It is pulled in transitively by
# libXft today, but the capability probe calls a public XRender entry point
# directly, so the dependency is declared here rather than being relied on by
# accident. A server without the extension is a warning and a degraded font
# ladder, never a build failure and never a fatal error.
pkg_check_modules(XRENDER REQUIRED IMPORTED_TARGET xrender)

# --- WM Binary (BLD-04) ---
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/Manager.cpp
    src/Client.cpp
    src/Border.cpp
    src/Buttons.cpp
    src/Events.cpp
    src/Config.cpp
    src/Rules.cpp
    src/AppCache.cpp
    src/DesktopEntry.cpp
    src/BinaryScanner.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${XFT_INCLUDE_DIRS}
    ${FONTCONFIG_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    PkgConfig::X11
    PkgConfig::XEXT
    PkgConfig::XFT
    PkgConfig::FONTCONFIG
    PkgConfig::XRANDR
    PkgConfig::XRENDER
    m
)

# --- Tests (TEST-01, TEST-02, TEST-04) ---
option(BUILD_TESTS "Build test suite" ON)

if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.14.0
    )
    FetchContent_MakeAvailable(Catch2)

    enable_testing()
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)

    # XTEST input synthesis (D-08, TEST-05). Test-only on purpose: the shipped
    # wm2-born-again target must NOT gain libXtst -- the 512MB VPS constraint
    # argues against dragging test-only libraries into the binary. Declared
    # inside if(BUILD_TESTS) and linked into test_wm_process only.
    pkg_check_modules(XTST REQUIRED IMPORTED_TARGET xtst)

    # --- Environment preflight fixture (D-01, D-02, TEST-07) ---
    #
    # Setup-only ctest fixture: fails with a named cause before any behavioral
    # assertion runs. Deliberately NOT a CMake configure-time check -- that would
    # block plain binary builds on machines that never run tests.
    #
    # The explicit LABELS keeps the phase on ONE selection mechanism. This is a
    # hand-written add_test, not a Catch2 case, so the tags-as-labels option does
    # not reach it; without the manual label the only way to select it would be
    # by name, reintroducing the exact name-vs-label split D-33 exists to remove.
    # Labelling a fixture test does not change fixture semantics: ctest still
    # pulls this setup test in automatically for any -L-selected test that
    # declares FIXTURES_REQUIRED preflight_ok, whether or not this test itself
    # matched the filter.
    add_test(NAME preflight COMMAND ${CMAKE_SOURCE_DIR}/scripts/preflight.sh --quiet)
    set_tests_properties(preflight PROPERTIES
        FIXTURES_SETUP preflight_ok
        LABELS "preflight"
    )

    # RAII unit tests (X11 linked for inline deleter calls, but no display needed)
    add_executable(test_raii tests/test_raii.cpp)
    target_link_libraries(test_raii PRIVATE
        Catch2::Catch2WithMain
        PkgConfig::X11
    )
    target_include_directories(test_raii PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${XFT_INCLUDE_DIRS}
    )
    # D-33: TAG SELECTION CONVENTION -- applies to every test-discovery call in
    # this file, no exceptions.
    #
    # Catch2 registers each ctest test under its TEST_CASE *name*, so a Catch2
    # tag is invisible to `ctest -R` (which matches names). A tag-shaped -R
    # expression matches zero tests, and ctest exits 0 on zero matches -- making
    # every such gate green-on-nothing. The tags-as-labels option (Catch2 v3.14
    # extras/Catch.cmake:152) registers the tags as ctest LABELS, fixing this at
    # the registration site rather than at each call site.
    #
    # Consequently, every gate in Phase 8 selects by ANCHORED label and always
    # passes --no-tests=error:
    #     ctest -L '^wm_process$' --no-tests=error
    #     ctest -L '^(wm_lifecycle|wm_sizehints|wm_gravity)$' --no-tests=error
    # Anchoring is load-bearing: -L is an unanchored regex, so a bare
    # `-L config` would also drag in `wm_config_runtime`. --no-tests=error is the
    # backstop that turns a mistyped or renamed tag into exit code 8 instead of a
    # silent pass.
    #
    # The option is orthogonal to PROPERTIES: Catch2 emits the labels through a
    # separate set_tests_properties call, so nothing already attached is
    # clobbered. Do NOT add the test-renaming prefix/suffix options -- they
    # rename tests out from under this convention and cannot express multi-tag
    # targets.
    #
    # NOTE: the acceptance gates for this convention are line-counting greps
    # (label-option lines must equal discovery-call lines; the shared-display
    # bindings must number exactly four). Keep those literal tokens out of
    # comment prose, or the comments defeat the guards they describe.
    catch_discover_tests(test_raii ADD_TAGS_AS_LABELS)

    # Smoke tests (need Xvfb display)
    find_program(XVFB_EXECUTABLE Xvfb)
    if(XVFB_EXECUTABLE)
        add_executable(test_smoke tests/test_smoke.cpp)
        target_link_libraries(test_smoke PRIVATE
            Catch2::Catch2WithMain
            PkgConfig::X11
        )
        target_include_directories(test_smoke PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${XFT_INCLUDE_DIRS}
        )

        # Xvfb fixture: start before tests, stop after.
        #
        # The shared-display group below additionally takes a named resource
        # lock (RESEARCH Pitfall 3). A running WM owns SubstructureRedirectMask
        # exclusively and reparents every window created on its display, which
        # would corrupt the raw-Xlib geometry and property assertions in
        # test_client / test_ewmh. test_wm_process allocates its own display and
        # never touches this one, but the lock makes it structurally impossible
        # for `ctest -j` to interleave these four targets with each other or
        # with any future WM-bearing target that mistakenly lands here.
        set(XVFB_PID_FILE "${CMAKE_BINARY_DIR}/xvfb.pid")
        set(XVFB_LOG_FILE "${CMAKE_BINARY_DIR}/xvfb99.log")

        # Two fixes over the original one-liner, both required for `ctest -j`:
        #   1. The backgrounded server's stdout/stderr go to a FILE. Previously
        #      it inherited ctest's output pipe and never closed it, so ctest
        #      blocked forever after the tests finished -- the whole suite hung.
        #   2. Readiness is a bounded poll on the server actually answering,
        #      not a flat `sleep 1` that can expire early under parallel load.
        add_test(NAME start_xvfb
            COMMAND bash -c "Xvfb :99 -screen 0 1024x768x24 -ac +render -noreset >${XVFB_LOG_FILE} 2>&1 & echo $! > ${XVFB_PID_FILE}; n=0; while [ $n -lt 100 ]; do DISPLAY=:99 xdpyinfo >/dev/null 2>&1 && exit 0; n=$((n+1)); sleep 0.1; done; echo 'wm2: Xvfb :99 did not become ready' >&2; exit 1"
        )
        set_tests_properties(start_xvfb PROPERTIES FIXTURES_SETUP xvfb_display)

        add_test(NAME stop_xvfb
            COMMAND bash -c "kill $(cat ${XVFB_PID_FILE}) 2>/dev/null || true"
        )
        set_tests_properties(stop_xvfb PROPERTIES FIXTURES_CLEANUP xvfb_display)

        # Discover smoke tests with fixture requirement set per-test
        catch_discover_tests(test_smoke ADD_TAGS_AS_LABELS
            PROPERTIES
                ENVIRONMENT "DISPLAY=:99"
                FIXTURES_REQUIRED "xvfb_display;preflight_ok"
                RESOURCE_LOCK "x_display_99"
        )
    else()
        message(WARNING "Xvfb not found -- smoke tests disabled")
    endif()

    # Event loop unit tests (need X11 headers but no display)
    add_executable(test_eventloop tests/test_eventloop.cpp)
    target_link_libraries(test_eventloop PRIVATE
        Catch2::Catch2WithMain
        PkgConfig::X11
    )
    target_include_directories(test_eventloop PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${XFT_INCLUDE_DIRS}
    )
    catch_discover_tests(test_eventloop ADD_TAGS_AS_LABELS)

    # Auto-raise timer unit tests (pure chrono arithmetic, no display needed)
    add_executable(test_autoraise tests/test_autoraise.cpp)
    target_link_libraries(test_autoraise PRIVATE
        Catch2::Catch2WithMain
    )
    target_include_directories(test_autoraise PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${XFT_INCLUDE_DIRS}
    )
    catch_discover_tests(test_autoraise ADD_TAGS_AS_LABELS)

    # Client lifecycle tests (need Xvfb display, TEST-03)
    if(XVFB_EXECUTABLE)
        add_executable(test_client tests/test_client.cpp)
        target_link_libraries(test_client PRIVATE
            Catch2::Catch2WithMain
            PkgConfig::X11
        )
        target_include_directories(test_client PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${XFT_INCLUDE_DIRS}
        )
        catch_discover_tests(test_client ADD_TAGS_AS_LABELS
            PROPERTIES
                ENVIRONMENT "DISPLAY=:99"
                FIXTURES_REQUIRED "xvfb_display;preflight_ok"
                RESOURCE_LOCK "x_display_99"
        )

        # Xft PoC test (need Xvfb display + Xft/fontconfig)
        add_executable(test_xft_poc tests/test_xft_poc.cpp)
        target_link_libraries(test_xft_poc PRIVATE
            Catch2::Catch2WithMain
            PkgConfig::X11
            PkgConfig::XEXT
            PkgConfig::XFT
            PkgConfig::FONTCONFIG
        )
        target_include_directories(test_xft_poc PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${XFT_INCLUDE_DIRS}
        )
        catch_discover_tests(test_xft_poc ADD_TAGS_AS_LABELS
            PROPERTIES
                ENVIRONMENT "DISPLAY=:99"
                FIXTURES_REQUIRED "xvfb_display;preflight_ok"
                RESOURCE_LOCK "x_display_99"
        )

        # EWMH compliance tests (need Xvfb display)
        add_executable(test_ewmh tests/test_ewmh.cpp)
        target_link_libraries(test_ewmh PRIVATE
            Catch2::Catch2WithMain
            PkgConfig::X11
        )
        target_include_directories(test_ewmh PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${XFT_INCLUDE_DIRS}
        )
        catch_discover_tests(test_ewmh ADD_TAGS_AS_LABELS
            PROPERTIES
                ENVIRONMENT "DISPLAY=:99"
                FIXTURES_REQUIRED "xvfb_display;preflight_ok"
                RESOURCE_LOCK "x_display_99"
        )

        # --- Process-level WM tests (D-07, TEST-05) ---
        #
        # Runs the REAL wm2-born-again binary as a child process. Deliberately
        # carries NO shared-display ENVIRONMENT binding and NO
        # `FIXTURES_REQUIRED xvfb_display`: this target allocates, locks and
        # tears down its own display per fixture instance. A running WM owns
        # SubstructureRedirectMask exclusively and would reparent the windows
        # test_client / test_ewmh assert on (RESEARCH Pitfall 3).
        add_executable(test_wm_process tests/test_wm_process.cpp)
        target_link_libraries(test_wm_process PRIVATE
            Catch2::Catch2WithMain
            PkgConfig::X11
            PkgConfig::XTST
        )
        target_include_directories(test_wm_process PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/tests
            ${XFT_INCLUDE_DIRS}
        )

        # Scratch directory for per-fixture display locks, captured child stderr,
        # and ASan log_path output. Under the build tree, never a bare fixed /tmp
        # name (threat T-8-TMP).
        set(WM_PROCESS_WORKDIR "${CMAKE_BINARY_DIR}/wm-process-tests")
        file(MAKE_DIRECTORY "${WM_PROCESS_WORKDIR}")

        target_compile_definitions(test_wm_process PRIVATE
            WM2_BINARY_PATH="$<TARGET_FILE:wm2-born-again>"
            WM2_LSAN_SUPPRESSIONS="${CMAKE_SOURCE_DIR}/tests/lsan.supp"
            WM2_TEST_WORKDIR="${WM_PROCESS_WORKDIR}"
        )

        # The tests exec the binary, so it must exist before they run.
        add_dependencies(test_wm_process wm2-born-again)

        # TIMEOUT is a structural backstop, not the primary mechanism: every
        # case here is already deadline-bounded internally, and the [wm_circulate]
        # case additionally has to prove a WEDGED WM fails rather than hangs.
        # If a future edit ever loses one of those internal deadlines, ctest kills
        # the case instead of stalling the whole suite. Generous against the
        # observed per-case cost (~11s here, more under ASan) so it can never be
        # the thing that reddens a healthy run.
        catch_discover_tests(test_wm_process ADD_TAGS_AS_LABELS
            PROPERTIES
                FIXTURES_REQUIRED preflight_ok
                TIMEOUT 240
        )

        # --- Degraded-capability tests (XDIS-03, TEST-05) ---
        #
        # Same shape as test_wm_process above -- own display per fixture
        # instance, so deliberately NO shared-display binding and NO
        # `FIXTURES_REQUIRED xvfb_display`.
        #
        # Two additions over that target. libXext, because the fallback
        # assertions ask the SERVER whether a frame carries a bounding shape
        # mask (a positive protocol-level check, not "nothing crashed"). And a
        # source-directory definition, because the single-funnel guard reads
        # src/Border.cpp as data and counts call sites in it.
        #
        # This target accumulates further tag groups as later plans in the
        # phase land their fallback coverage. That is the point of selecting by
        # per-test label rather than by target name (D-33): one file can carry
        # several unrelated capability groups without any of the gates
        # over-selecting.
        add_executable(test_wm_fallbacks tests/test_wm_fallbacks.cpp)
        target_link_libraries(test_wm_fallbacks PRIVATE
            Catch2::Catch2WithMain
            PkgConfig::X11
            PkgConfig::XEXT
            # XDIS-04: the [xft_norender_spike] group opens the same rotated
            # FcMatrix pattern the production loader opens and asks the public
            # XRender entry point whether the extension is there. Both are
            # linked explicitly rather than leaning on libXft's transitive
            # dependencies, so the spike cannot start answering a different
            # question if that transitive edge ever changes.
            PkgConfig::XFT
            PkgConfig::FONTCONFIG
            PkgConfig::XRENDER
        )
        target_include_directories(test_wm_fallbacks PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/tests
            ${XFT_INCLUDE_DIRS}
            ${FONTCONFIG_INCLUDE_DIRS}
        )

        target_compile_definitions(test_wm_fallbacks PRIVATE
            WM2_BINARY_PATH="$<TARGET_FILE:wm2-born-again>"
            WM2_LSAN_SUPPRESSIONS="${CMAKE_SOURCE_DIR}/tests/lsan.supp"
            WM2_TEST_WORKDIR="${WM_PROCESS_WORKDIR}"
            WM2_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )

        # The tests exec the binary, so it must exist before they run.
        add_dependencies(test_wm_fallbacks wm2-born-again)

        catch_discover_tests(test_wm_fallbacks ADD_TAGS_AS_LABELS
            PROPERTIES
                FIXTURES_REQUIRED preflight_ok
        )

        # --- Screen-geometry tests (XDIS-01, D-27) ---
        #
        # Same shape as the two targets above -- own display per fixture
        # instance, so deliberately NO shared-display binding and NO
        # `FIXTURES_REQUIRED xvfb_display`. Unlike test_wm_fallbacks this one
        # needs no extra X extension library: every assertion is core-protocol
        # geometry read back from the server.
        #
        # The suite runs its server at 1280x1024 (requested from the test file
        # through the fixture's extra-Xvfb-argument hook, not from here), which
        # is the largest geometry any Phase 8 test needs. Plan 08-05 shrinks
        # from that size to observe a resolution change; this server's RANDR
        # maximum equals the size it was started with, so it can never grow.
        add_executable(test_wm_geometry tests/test_wm_geometry.cpp)
        # XTEST is here for the drag cases only: the tab-reachability clamp can
        # only be proven by a real pointer drag, and the fake events must come
        # from a connection with grab control enabled (RESEARCH Pitfall 10),
        # because Client::move() holds a pointer grab for the whole drag.
        target_link_libraries(test_wm_geometry PRIVATE
            Catch2::Catch2WithMain
            PkgConfig::X11
            PkgConfig::XTST
        )
        target_include_directories(test_wm_geometry PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/tests
            ${XFT_INCLUDE_DIRS}
        )

        target_compile_definitions(test_wm_geometry PRIVATE
            WM2_BINARY_PATH="$<TARGET_FILE:wm2-born-again>"
            WM2_LSAN_SUPPRESSIONS="${CMAKE_SOURCE_DIR}/tests/lsan.supp"
            WM2_TEST_WORKDIR="${WM_PROCESS_WORKDIR}"
        )

        # The tests exec the binary, so it must exist before they run.
        add_dependencies(test_wm_geometry wm2-born-again)

        # ADD_TAGS_AS_LABELS is required by D-33, or the tag this file carries
        # is not selectable by the anchored label gates the phase uses.
        catch_discover_tests(test_wm_geometry ADD_TAGS_AS_LABELS
            PROPERTIES
                FIXTURES_REQUIRED preflight_ok
        )

        # --- Focus-policy tests (FOCUS-02, TEST-05) ---
        #
        # Cloned from test_wm_geometry above -- own display per fixture
        # instance, so deliberately NO shared-display binding and NO
        # `FIXTURES_REQUIRED xvfb_display`.
        #
        # One addition over that target: the XTEST library. Focus is driven by
        # real pointer input here, and it must come from a connection with grab
        # control enabled (RESEARCH Pitfall 10) -- the WM takes pointer grabs
        # during interaction, and Client::eventButton() ignores synthetic
        # events outright, so nothing else reaches the paths under test.
        add_executable(test_wm_focus tests/test_wm_focus.cpp)
        target_link_libraries(test_wm_focus PRIVATE
            Catch2::Catch2WithMain
            PkgConfig::X11
            PkgConfig::XTST
        )
        target_include_directories(test_wm_focus PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/tests
            ${XFT_INCLUDE_DIRS}
        )

        target_compile_definitions(test_wm_focus PRIVATE
            WM2_BINARY_PATH="$<TARGET_FILE:wm2-born-again>"
            WM2_LSAN_SUPPRESSIONS="${CMAKE_SOURCE_DIR}/tests/lsan.supp"
            WM2_TEST_WORKDIR="${WM_PROCESS_WORKDIR}"
        )

        # The tests exec the binary, so it must exist before they run.
        add_dependencies(test_wm_focus wm2-born-again)

        # ADD_TAGS_AS_LABELS is required by D-33, or [wm_focus] is not
        # selectable by the anchored label gates this plan's criteria use.
        catch_discover_tests(test_wm_focus ADD_TAGS_AS_LABELS
            PROPERTIES
                FIXTURES_REQUIRED preflight_ok
                TIMEOUT 240
        )

        # --- Window-rule application tests (RULES-02) ---
        #
        # Cloned from test_wm_focus above, minus XTEST: nothing here is driven by
        # pointer input. Rules reach the WM through a real config file in an
        # isolated XDG tree handed to the child, so this target needs no
        # additional link or definition beyond the fixture's three.
        #
        # Own display per fixture instance, so deliberately NO shared-display
        # binding and NO `FIXTURES_REQUIRED xvfb_display`.
        add_executable(test_wm_rules tests/test_wm_rules.cpp)
        target_link_libraries(test_wm_rules PRIVATE
            Catch2::Catch2WithMain
            PkgConfig::X11
        )
        target_include_directories(test_wm_rules PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/tests
            ${XFT_INCLUDE_DIRS}
        )

        target_compile_definitions(test_wm_rules PRIVATE
            WM2_BINARY_PATH="$<TARGET_FILE:wm2-born-again>"
            WM2_LSAN_SUPPRESSIONS="${CMAKE_SOURCE_DIR}/tests/lsan.supp"
            WM2_TEST_WORKDIR="${WM_PROCESS_WORKDIR}"
        )

        # The tests exec the binary, so it must exist before they run.
        add_dependencies(test_wm_rules wm2-born-again)

        # ADD_TAGS_AS_LABELS is required by D-33, or the tag this file carries
        # is not selectable by the anchored label gates this plan's criteria use.
        catch_discover_tests(test_wm_rules ADD_TAGS_AS_LABELS
            PROPERTIES
                FIXTURES_REQUIRED preflight_ok
                TIMEOUT 240
        )

        # --- Lifecycle, size-hint and gravity tests (TEST-05, plan 08-11) ---
        #
        # Cloned from test_wm_rules above, plus XTEST: the destroy-while-focus-
        # tracking case needs a real pointer entry to put focus tracking in
        # flight, and a synthetic event would not reach that path
        # (RESEARCH Pitfall 10, and Client::eventButton ignores send_event).
        # The size-hint group drives real resize drags on the frame's resize
        # handle for the same reason -- an interactive resize is the path the
        # constraint function sits on, and a configure request takes a different
        # route entirely.
        #
        # Own display per fixture instance, so deliberately NO shared-display
        # binding and NO `FIXTURES_REQUIRED xvfb_display`.
        add_executable(test_wm_lifecycle tests/test_wm_lifecycle.cpp)
        target_link_libraries(test_wm_lifecycle PRIVATE
            Catch2::Catch2WithMain
            PkgConfig::X11
            PkgConfig::XTST
        )
        target_include_directories(test_wm_lifecycle PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/tests
            ${XFT_INCLUDE_DIRS}
        )

        target_compile_definitions(test_wm_lifecycle PRIVATE
            WM2_BINARY_PATH="$<TARGET_FILE:wm2-born-again>"
            WM2_LSAN_SUPPRESSIONS="${CMAKE_SOURCE_DIR}/tests/lsan.supp"
            WM2_TEST_WORKDIR="${WM_PROCESS_WORKDIR}"
        )

        # The tests exec the binary, so it must exist before they run.
        add_dependencies(test_wm_lifecycle wm2-born-again)

        # ADD_TAGS_AS_LABELS is required by D-33, or the three tags this file
        # carries are not selectable by the anchored label gates.
        catch_discover_tests(test_wm_lifecycle ADD_TAGS_AS_LABELS
            PROPERTIES
                FIXTURES_REQUIRED preflight_ok
                TIMEOUT 300
        )

        # --- EWMH state, fullscreen/maximize and malformed-property tests
        #     (TEST-05, plan 08-12) ---
        #
        # Cloned from test_wm_lifecycle above. XTEST is linked for the same
        # reason it is there -- the fullscreen/maximize group needs the pointer
        # parked and moved through real device events rather than warps that the
        # WM's crossing handlers would treat differently -- and so that a later
        # case in this file can reach for a real click without a build change.
        #
        # Own display per fixture instance, so deliberately NO shared-display
        # binding and NO `FIXTURES_REQUIRED xvfb_display`.
        add_executable(test_wm_state tests/test_wm_state.cpp)
        target_link_libraries(test_wm_state PRIVATE
            Catch2::Catch2WithMain
            PkgConfig::X11
            PkgConfig::XTST
        )
        target_include_directories(test_wm_state PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/tests
            ${XFT_INCLUDE_DIRS}
        )

        target_compile_definitions(test_wm_state PRIVATE
            WM2_BINARY_PATH="$<TARGET_FILE:wm2-born-again>"
            WM2_LSAN_SUPPRESSIONS="${CMAKE_SOURCE_DIR}/tests/lsan.supp"
            WM2_TEST_WORKDIR="${WM_PROCESS_WORKDIR}"
        )

        # The tests exec the binary, so it must exist before they run.
        add_dependencies(test_wm_state wm2-born-again)

        # ADD_TAGS_AS_LABELS is required by D-33, or the three tags this file
        # carries are not selectable by the anchored label gates.
        catch_discover_tests(test_wm_state ADD_TAGS_AS_LABELS
            PROPERTIES
                FIXTURES_REQUIRED preflight_ok
                TIMEOUT 300
        )

        # --- Config-to-runtime, terminating error paths and stress
        #     (TEST-05, plan 08-13) ---
        #
        # Cloned from test_wm_state above. XTEST is linked because the config
        # group drives the root menu and the tab button with real device events:
        # the menu and the long-press delete both run inside NESTED event loops
        # that hold a pointer grab, and only a connection with grab control
        # enabled can reach them.
        #
        # Own display per fixture instance, so deliberately NO shared-display
        # binding and NO `FIXTURES_REQUIRED xvfb_display`.
        add_executable(test_wm_runtime tests/test_wm_runtime.cpp)
        target_link_libraries(test_wm_runtime PRIVATE
            Catch2::Catch2WithMain
            PkgConfig::X11
            PkgConfig::XTST
        )
        target_include_directories(test_wm_runtime PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/tests
            ${XFT_INCLUDE_DIRS}
        )

        target_compile_definitions(test_wm_runtime PRIVATE
            WM2_BINARY_PATH="$<TARGET_FILE:wm2-born-again>"
            WM2_LSAN_SUPPRESSIONS="${CMAKE_SOURCE_DIR}/tests/lsan.supp"
            WM2_TEST_WORKDIR="${WM_PROCESS_WORKDIR}"
        )

        # The tests exec the binary, so it must exist before they run.
        add_dependencies(test_wm_runtime wm2-born-again)

        # ADD_TAGS_AS_LABELS is required by D-33, or the four tags this file
        # carries are not selectable by the anchored label gates.
        #
        # A longer TIMEOUT than its siblings: the stress group churns 100+
        # windows against the WM and the config group starts eleven separate
        # fixtures, each of which pays a full Xvfb + WM startup. Under the
        # sanitizer that is roughly twice as slow again.
        catch_discover_tests(test_wm_runtime ADD_TAGS_AS_LABELS
            PROPERTIES
                FIXTURES_REQUIRED preflight_ok
                TIMEOUT 600
        )

        # --- Resource budget: resident memory and idle CPU (D-32, TEST-08,
        #     plan 08-14) ---
        #
        # Cloned from test_wm_runtime above, minus XTEST: this file drives no
        # device events at all. It deliberately issues NO X traffic during its
        # idle windows, because any request would be work the WM is then charged
        # for by the very sample that is supposed to find it idle.
        #
        # Own display per fixture instance, so deliberately NO shared-display
        # binding and NO `FIXTURES_REQUIRED xvfb_display`.
        add_executable(test_wm_resource tests/test_wm_resource.cpp)
        target_link_libraries(test_wm_resource PRIVATE
            Catch2::Catch2WithMain
            PkgConfig::X11
        )
        target_include_directories(test_wm_resource PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/tests
            ${XFT_INCLUDE_DIRS}
        )

        target_compile_definitions(test_wm_resource PRIVATE
            WM2_BINARY_PATH="$<TARGET_FILE:wm2-born-again>"
            WM2_LSAN_SUPPRESSIONS="${CMAKE_SOURCE_DIR}/tests/lsan.supp"
            WM2_TEST_WORKDIR="${WM_PROCESS_WORKDIR}"
        )

        # The tests exec the binary, so it must exist before they run.
        add_dependencies(test_wm_resource wm2-born-again)

        # ADD_TAGS_AS_LABELS is required by D-33, or the [wm_resource_budget]
        # tag is not selectable by the anchored label gate
        # `ctest -L '^wm_resource_budget$'` -- which would then match NOTHING and,
        # with --no-tests=error, fail loudly rather than certify an empty set.
        #
        # The TIMEOUT is generous for a specific, non-negotiable reason: each of
        # the three cases pays a full Xvfb + WM startup, maps twenty clients, and
        # two of them then sit through a THIRTY-SECOND idle window that cannot be
        # shortened without coarsening the one-percent bar. Under the sanitizer,
        # in a loaded full-suite run, that is several times slower again. A
        # timeout here exists to stop a wedged case hanging the suite forever, not
        # to assert that any of this is fast.
        catch_discover_tests(test_wm_resource ADD_TAGS_AS_LABELS
            PROPERTIES
                FIXTURES_REQUIRED preflight_ok
                TIMEOUT 600
        )
    endif()

    # Config parsing unit tests (pure data transformation, no X11 needed)
    add_executable(test_config tests/test_config.cpp src/Config.cpp)
    target_link_libraries(test_config PRIVATE
        Catch2::Catch2WithMain
    )
    target_include_directories(test_config PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    catch_discover_tests(test_config ADD_TAGS_AS_LABELS)

    # --- Window-rule model, matcher and parser unit tests (RULES-01) ---
    #
    # Display-free BY CONSTRUCTION, not by luck: the rules header and its
    # implementation carry no X11 include of any kind, so this target links
    # neither PkgConfig::X11 nor any display fixture and declares no
    # ENVIRONMENT binding. That is the whole reason the rules model is plain
    # data -- every parser and matcher edge case is reachable in milliseconds
    # with no X server, so this suite is cheap enough to run on every commit.
    #
    # Sources are compiled directly into the executable (the test_config
    # pattern immediately above) rather than linked from a library, because the
    # project ships no library target. The config implementation is compiled in
    # because the parser half of RULES-01 lives in Config::applyKeyValue().
    #
    # The acceptance guard for this plan counts literal source-path occurrences
    # in this file (the WM source list plus this target), so those paths are
    # kept out of the prose above on purpose.
    add_executable(test_rules tests/test_rules.cpp src/Rules.cpp src/Config.cpp)
    target_link_libraries(test_rules PRIVATE
        Catch2::Catch2WithMain
    )
    target_include_directories(test_rules PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    # D-33: ADD_TAGS_AS_LABELS is required, or the tag this file carries is not
    # selectable by the anchored label gates this plan's criteria use.
    catch_discover_tests(test_rules ADD_TAGS_AS_LABELS)

    # DesktopEntry (.desktop) parsing unit tests (pure data transformation, no X11 needed)
    add_executable(test_desktopentry tests/test_desktopentry.cpp src/DesktopEntry.cpp)
    target_link_libraries(test_desktopentry PRIVATE
        Catch2::Catch2WithMain
    )
    target_include_directories(test_desktopentry PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    catch_discover_tests(test_desktopentry ADD_TAGS_AS_LABELS)

    # BinaryScanner (ELF DT_NEEDED) unit tests (pure data transformation, no X11 needed)
    add_executable(test_binaryscanner tests/test_binaryscanner.cpp src/BinaryScanner.cpp)
    target_link_libraries(test_binaryscanner PRIVATE
        Catch2::Catch2WithMain
    )
    target_include_directories(test_binaryscanner PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    catch_discover_tests(test_binaryscanner ADD_TAGS_AS_LABELS)

    # AppCache (hand-rolled JSON cache + merge/rescan orchestration) unit
    # tests (pure data transformation, no X11 needed). Links DesktopEntry.cpp
    # and BinaryScanner.cpp since loadOrRescan()'s rescan path calls both.
    add_executable(test_appcache tests/test_appcache.cpp src/AppCache.cpp src/DesktopEntry.cpp src/BinaryScanner.cpp)
    target_link_libraries(test_appcache PRIVATE
        Catch2::Catch2WithMain
    )
    target_include_directories(test_appcache PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    catch_discover_tests(test_appcache ADD_TAGS_AS_LABELS)
endif()
