#include "Manager.h"
#include "Config.h"
#include "AppCache.h"
#include <cstdio>

int main(int argc, char** argv) {
    Config config = Config::load(argc, argv);
    // The AUTO-DISCOVERED list only. The window manager merges the config's
    // manual entries onto it itself (plan 09-05), because those entries can now
    // change while it is running and the merge therefore has to be re-runnable
    // from inputs it holds.
    std::vector<AppEntry> apps = AppCache::loadAutoDiscovered(AppCache::defaultCachePath());
    // argc/argv are handed on so `reload` can re-run the same layered load,
    // command-line layer included (DISC-07). Without them a reload would let
    // the config file override a value the user gave on the command line.
    WindowManager manager(config, apps, argc, argv);
    return 0;
}
