#include <catch2/catch_test_macros.hpp>

#include "AppCache.h"
#include "AppEntry.h"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>

// Helper: unique temp cache path per test invocation.
static std::string tempCachePath() {
    static int counter = 0;
    return "/tmp/wm2-test-appcache-" + std::to_string(++counter) + ".json";
}

// =============================================================================
// Test (a): write-then-read round-trip preserves all fields for mixed sources
// =============================================================================
TEST_CASE("AppCache write-then-read round-trips mixed-source entries", "[appcache]") {
    std::string path = tempCachePath();

    AppCache::CacheData data;
    data.scannedAt = 1700000000;

    AppEntry desktop;
    desktop.name = "Firefox";
    desktop.execArgv = {"firefox"};
    desktop.icon = "firefox-icon";
    desktop.category = "Internet";
    desktop.source = AppEntry::Source::Desktop;
    data.entries.push_back(desktop);

    AppEntry binaryScan;
    binaryScan.name = "xeyes";
    binaryScan.execArgv = {"xeyes", "-bg", "white"};
    binaryScan.icon = "";
    binaryScan.category = "Other";
    binaryScan.source = AppEntry::Source::BinaryScan;
    data.entries.push_back(binaryScan);

    AppCache::write(path, data);
    AppCache::CacheData readBack = AppCache::read(path);

    REQUIRE(readBack.scannedAt == 1700000000);
    REQUIRE(readBack.entries.size() == 2);

    REQUIRE(readBack.entries[0].name == "Firefox");
    REQUIRE(readBack.entries[0].execArgv == std::vector<std::string>{"firefox"});
    REQUIRE(readBack.entries[0].icon == "firefox-icon");
    REQUIRE(readBack.entries[0].category == "Internet");
    REQUIRE(readBack.entries[0].source == AppEntry::Source::Desktop);

    REQUIRE(readBack.entries[1].name == "xeyes");
    REQUIRE(readBack.entries[1].execArgv == (std::vector<std::string>{"xeyes", "-bg", "white"}));
    REQUIRE(readBack.entries[1].icon == "");
    REQUIRE(readBack.entries[1].category == "Other");
    REQUIRE(readBack.entries[1].source == AppEntry::Source::BinaryScan);

    std::remove(path.c_str());
}

// =============================================================================
// Test (b): read() on a nonexistent path returns an empty CacheData
// =============================================================================
TEST_CASE("AppCache read on nonexistent path returns empty CacheData", "[appcache]") {
    AppCache::CacheData data = AppCache::read("/tmp/wm2-appcache-does-not-exist-xyz123.json");
    REQUIRE(data.scannedAt == 0);
    REQUIRE(data.entries.empty());
}

// =============================================================================
// Test (c): mergeEntries D-08 override -- name match replaces entirely
// =============================================================================
TEST_CASE("mergeEntries D-08 override replaces name-matching entry entirely", "[appcache]") {
    AppEntry autoFirefox;
    autoFirefox.name = "Firefox";
    autoFirefox.category = "Internet";
    autoFirefox.execArgv = {"firefox"};
    autoFirefox.source = AppEntry::Source::Desktop;

    AppEntry manualFirefox;
    manualFirefox.name = "Firefox";
    manualFirefox.execArgv = {"firefox", "--private-window"};
    manualFirefox.category = "Custom";
    manualFirefox.source = AppEntry::Source::Manual;

    auto merged = AppCache::mergeEntries({autoFirefox}, {manualFirefox});

    REQUIRE(merged.size() == 1);
    REQUIRE(merged[0].name == "Firefox");
    REQUIRE(merged[0].execArgv == (std::vector<std::string>{"firefox", "--private-window"}));
    REQUIRE(merged[0].source == AppEntry::Source::Manual);
}

// =============================================================================
// Test (d): mergeEntries D-07 default -- Custom category preserved, no
// auto-discovered name collision.
// =============================================================================
TEST_CASE("mergeEntries D-07 default Custom category preserved for non-colliding manual entry", "[appcache]") {
    AppEntry autoFirefox;
    autoFirefox.name = "Firefox";
    autoFirefox.category = "Internet";
    autoFirefox.source = AppEntry::Source::Desktop;

    AppEntry customApp;
    customApp.name = "Custom App";
    customApp.category = "Custom";  // already defaulted by caller (Config::applyKeyValue)
    customApp.source = AppEntry::Source::Manual;

    auto merged = AppCache::mergeEntries({autoFirefox}, {customApp});

    REQUIRE(merged.size() == 2);
    REQUIRE(merged[0].name == "Firefox");
    REQUIRE(merged[1].name == "Custom App");
    REQUIRE(merged[1].category == "Custom");
}

// =============================================================================
// Test (e): mergeEntries preserves an explicit non-Custom category on a
// manual entry (not overwritten back to Custom).
// =============================================================================
TEST_CASE("mergeEntries preserves explicit non-Custom category on manual entry", "[appcache]") {
    AppEntry onlyManual;
    onlyManual.name = "Only Manual";
    onlyManual.category = "Graphics";
    onlyManual.source = AppEntry::Source::Manual;

    auto merged = AppCache::mergeEntries({}, {onlyManual});

    REQUIRE(merged.size() == 1);
    REQUIRE(merged[0].name == "Only Manual");
    REQUIRE(merged[0].category == "Graphics");
}

// =============================================================================
// Test (f): needsRescan() epoch-vs-far-future sanity checks
// =============================================================================
TEST_CASE("needsRescan returns true for epoch and false for far-future timestamp", "[appcache]") {
    REQUIRE(AppCache::needsRescan(0) == true);
    REQUIRE(AppCache::needsRescan(std::time(nullptr) + 1000000) == false);
}

// =============================================================================
// Additional coverage: malformed cache entries are skipped with a warning,
// not aborting the whole read.
// =============================================================================
TEST_CASE("AppCache read skips a malformed entry but keeps well-formed ones", "[appcache]") {
    std::string path = tempCachePath();
    {
        std::ofstream out(path);
        out << "{\"scannedAt\": 42, \"entries\": ["
            << "{\"name\": \"Good\", \"exec\": [\"good\"], \"icon\": \"\", \"category\": \"Custom\", \"source\": \"manual\"}, "
            << "{\"icon\": \"\", \"category\": \"Custom\", \"source\": \"manual\"}"  // missing name/exec
            << "]}";
    }

    AppCache::CacheData data = AppCache::read(path);
    REQUIRE(data.scannedAt == 42);
    REQUIRE(data.entries.size() == 1);
    REQUIRE(data.entries[0].name == "Good");

    std::remove(path.c_str());
}

// =============================================================================
// Additional coverage: loadOrRescan() persists a fresh cache and the second
// call takes the fast (no-rescan) path.
// =============================================================================
TEST_CASE("loadOrRescan performs a full rescan, persists, then fast-paths", "[appcache]") {
    std::string path = tempCachePath();
    std::remove(path.c_str());

    AppEntry manual;
    manual.name = "ZZZ Test Manual Entry";
    manual.category = "Custom";
    manual.source = AppEntry::Source::Manual;
    manual.execArgv = {"true"};

    auto result = AppCache::loadOrRescan(path, {manual});

    bool foundManual = false;
    for (const auto& e : result) {
        if (e.name == "ZZZ Test Manual Entry") foundManual = true;
    }
    REQUIRE(foundManual);

    AppCache::CacheData cache = AppCache::read(path);
    REQUIRE(cache.scannedAt > 0);
    REQUIRE(AppCache::needsRescan(cache.scannedAt) == false);

    std::remove(path.c_str());
}
