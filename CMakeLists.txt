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

# --- WM Binary (BLD-04) ---
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/Manager.cpp
    src/Client.cpp
    src/Border.cpp
    src/Buttons.cpp
    src/Events.cpp
    src/Config.cpp
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

        # Xvfb fixture: start before tests, stop after
        set(XVFB_PID_FILE "${CMAKE_BINARY_DIR}/xvfb.pid")
        add_test(NAME start_xvfb
            COMMAND bash -c "Xvfb :99 -screen 0 1024x768x24 -ac +render -noreset & echo $! > ${XVFB_PID_FILE} && sleep 1"
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
                FIXTURES_REQUIRED xvfb_display
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
                FIXTURES_REQUIRED xvfb_display
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
                FIXTURES_REQUIRED xvfb_display
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
                FIXTURES_REQUIRED xvfb_display
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

        catch_discover_tests(test_wm_process ADD_TAGS_AS_LABELS)
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
