#pragma once
#include <string>

// Runtime game settings, loaded from settings.conf (an INI-style file).
// Every option can be changed by editing that single file and relaunching.
struct Settings {
    // --- Window ---
    int windowWidth = 1280;
    int windowHeight = 720;
    int windowVsync = 1;      // 0=off, 1=on
    int windowBorderless = 0; // 0=windowed, 1=borderless fullscreen

    // --- Graphics ---
    int renderDistance = 36;  // chunks in each direction from the player
    float fovDegrees = 70.0f;
    int maxFps = 0;           // 0 = unlimited (vsync off)

    // --- Gameplay ---
    float reach = 6.0f;       // block breaking/placing distance
    float breakTime = 2.0f;   // seconds to destroy a block while holding LMB
    float placeDelay = 0.125f; // min seconds between block placements while holding RMB

    // --- Quality ---
    int maxAnisotropy = 8;    // texture filtering quality
    int meshBudget = 32;      // chunks remeshed per frame (higher = faster load)

    // Loads settings.conf from the working dir or next to the executable.
    // Missing file -> defaults. Returns true if a file was found.
    bool loadFile(const char* path);
    void load(const char* exeDirPathBase); // resolve both locations
};

// Parses "key=value" lines, skipping blank lines and # comments.
void settingsApply(Settings& s, const std::string& line);
