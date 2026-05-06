#include "Manager.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc > 1) {
        std::fprintf(stderr, "usage: %s\n", argv[0]);
        return 2;
    }
    WindowManager manager;
    return 0;
}
