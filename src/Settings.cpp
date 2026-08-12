#include "Settings.h"
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>

namespace {
static int parseInt(const std::string& v) { return std::atoi(v.c_str()); }
static float parseFloat(const std::string& v) { return (float)std::atof(v.c_str()); }
static void trim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c) {
        return !std::isspace(c);
    }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) {
        return !std::isspace(c);
    }).base(), s.end());
}
}

void settingsApply(Settings& s, const std::string& line) {
    std::string key, val;
    std::istringstream ss(line);
    std::getline(ss, key, '=');
    std::getline(ss, val);
    trim(key);
    trim(val);
    if (key.empty()) return;

    if      (key == "windowWidth")      s.windowWidth = parseInt(val);
    else if (key == "windowHeight")     s.windowHeight = parseInt(val);
    else if (key == "windowVsync")      s.windowVsync = parseInt(val);
    else if (key == "windowBorderless") s.windowBorderless = parseInt(val);
    else if (key == "renderDistance")   s.renderDistance = parseInt(val);
    else if (key == "fov")              s.fovDegrees = parseFloat(val);
    else if (key == "maxFps")           s.maxFps = parseInt(val);
    else if (key == "reach")            s.reach = parseFloat(val);
    else if (key == "breakTime")        s.breakTime = parseFloat(val);
    else if (key == "placeDelay")       s.placeDelay = parseFloat(val);
    else if (key == "maxAnisotropy")    s.maxAnisotropy = parseInt(val);
    else if (key == "meshBudget")       s.meshBudget = parseInt(val);
}

bool Settings::loadFile(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        settingsApply(*this, line);
    }
    return true;
}

void Settings::load(const char* exeDir) {
    // 1) settings.conf in the working directory overrides everything.
    if (loadFile("settings.conf")) return;
    // 2) Fall back to next to the executable.
    if (exeDir) {
        std::string p = std::string(exeDir) + "/settings.conf";
        loadFile(p.c_str());
    }
}
