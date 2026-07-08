#include "Manager.h"
#include "Config.h"
#include "AppCache.h"
#include <cstdio>

int main(int argc, char** argv) {
    Config config = Config::load(argc, argv);
    std::vector<AppEntry> apps = AppCache::loadOrRescan(AppCache::defaultCachePath(), config.manualMenuEntries);
    WindowManager manager(config, apps);
    return 0;
}
