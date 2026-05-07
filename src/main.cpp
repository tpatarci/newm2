#include "Manager.h"
#include "Config.h"
#include <cstdio>

int main(int argc, char** argv) {
    Config config = Config::load(argc, argv);
    WindowManager manager(config);
    return 0;
}
