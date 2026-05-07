cmake_minimum_required(VERSION 3.16)
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
    upstream-wm2/Rotated.C
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/upstream-wm2
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

# Relax warnings for third-party xvertext code (Rotated.C is ~1992 era)
set_source_files_properties(upstream-wm2/Rotated.C PROPERTIES
    COMPILE_FLAGS "-w"
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
    catch_discover_tests(test_raii)

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
        catch_discover_tests(test_smoke
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
    catch_discover_tests(test_eventloop)

    # Auto-raise timer unit tests (pure chrono arithmetic, no display needed)
    add_executable(test_autoraise tests/test_autoraise.cpp)
    target_link_libraries(test_autoraise PRIVATE
        Catch2::Catch2WithMain
    )
    target_include_directories(test_autoraise PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${XFT_INCLUDE_DIRS}
    )
    catch_discover_tests(test_autoraise)

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
        catch_discover_tests(test_client
            PROPERTIES
                ENVIRONMENT "DISPLAY=:99"
                FIXTURES_REQUIRED xvfb_display
        )

        # Xft PoC test (need Xvfb display + Xft/fontconfig)
        add_executable(test_xft_poc tests/test_xft_poc.cpp)
        target_link_libraries(test_xft_poc PRIVATE
            Catch2::Catch2WithMain
            PkgConfig::X11
            PkgConfig::XFT
            PkgConfig::FONTCONFIG
        )
        target_include_directories(test_xft_poc PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${XFT_INCLUDE_DIRS}
        )
        catch_discover_tests(test_xft_poc
            PROPERTIES
                ENVIRONMENT "DISPLAY=:99"
                FIXTURES_REQUIRED xvfb_display
        )
    endif()
endif()
