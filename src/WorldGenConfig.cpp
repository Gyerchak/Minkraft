#include "WorldGenConfig.h"
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <string>

namespace {
static float f(const std::string& v) { return (float)std::atof(v.c_str()); }
static int i(const std::string& v) { return std::atoi(v.c_str()); }
static void trim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c) {
        return !std::isspace(c);
    }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) {
        return !std::isspace(c);
    }).base(), s.end());
}
}

void worldGenApply(WorldGenConfig& c, const std::string& line) {
    std::string key, val;
    std::istringstream ss(line);
    std::getline(ss, key, '=');
    std::getline(ss, val);
    trim(key);
    trim(val);
    if (key.empty()) return;

    if      (key == "baseHeight")      c.baseHeight = f(val);
    else if (key == "heightAmp")       c.heightAmp = f(val);
    else if (key == "terrainScale")    c.terrainScale = f(val);
    else if (key == "terrainOctaves")  c.terrainOctaves = i(val);
    else if (key == "hillScale")       c.hillScale = f(val);
    else if (key == "hillAmp")         c.hillAmp = f(val);
    else if (key == "hillOctaves")     c.hillOctaves = i(val);
    else if (key == "mountainThreshold") c.mountainThreshold = f(val);
    else if (key == "mountainAmp")     c.mountainAmp = f(val);
    else if (key == "mountainScale")   c.mountainScale = f(val);
    else if (key == "mountainOctaves") c.mountainOctaves = i(val);
    else if (key == "caveThreshold")   c.caveThreshold = f(val);
    else if (key == "seaLevel")        c.seaLevel = i(val);
    else if (key == "snowHeight")      c.snowHeight = i(val);
    else if (key == "beachLevel")      c.beachLevel = i(val);
    else if (key == "treeChancePct")   c.treeChancePct = i(val);
    else if (key == "treeMinHeight")   c.treeMinHeight = i(val);
    else if (key == "treeMaxHeight")   c.treeMaxHeight = i(val);
}

bool WorldGenConfig::loadFile(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        worldGenApply(*this, line);
    }
    return true;
}

void WorldGenConfig::load(const char* exeDir) {
    if (loadFile("worldgen.conf")) return;
    if (exeDir) {
        std::string p = std::string(exeDir) + "/worldgen.conf";
        loadFile(p.c_str());
    }
}
