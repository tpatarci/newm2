#define _GNU_SOURCE

#include "Config.h"

#include <getopt.h>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>

// XDG path resolution

std::string xdgConfigHome() {
    // TODO: implement
    return "";
}

std::vector<std::string> xdgConfigDirs() {
    // TODO: implement
    return {};
}

void Config::applyFile(const std::string& path) {
    // TODO: implement
}

void Config::applyKeyValue(const std::string& key, const std::string& value) {
    // TODO: implement
}

void Config::applyCliArgs(int argc, char** argv) {
    // TODO: implement - stub for now (Plan 02 implements CLI)
}

Config Config::load(int argc, char** argv) {
    // TODO: implement
    Config cfg;
    return cfg;
}
