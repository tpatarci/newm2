#include "Manager.h"
#include "Config.h"
#include "AppCache.h"
#include <cstdio>

int main(int argc, char** argv) {
    Config config = Config::load(argc, argv);
    std::vector<AppEntry> apps = AppCache::loadOrRescan(AppCache::defaultCachePath(), config.manualMenuEntries);
    // argc/argv are handed on so `reload` can re-run the same layered load,
    // command-line layer included (DISC-07). Without them a reload would let
    // the config file override a value the user gave on the command line.
    WindowManager manager(config, apps, argc, argv);
    return 0;
}
