#pragma once
#include <string>

// World generation rules, loaded from worldgen.conf (an INI-style file).
// Every option can be changed by editing that single file and relaunching.
struct WorldGenConfig {
    // --- Base rolling ground ---
    float baseHeight = 16.0f;    // baseline land height
    float heightAmp = 26.0f;     // how much the rolling terrain undulates
    float terrainScale = 0.010f; // 1/hill width (smaller = wider hills)
    int terrainOctaves = 4;

    // --- Bumpy detail hills ---
    float hillScale = 0.030f;
    float hillAmp = 7.0f;
    int hillOctaves = 3;

    // --- Mountains ---
    float mountainThreshold = 0.50f; // noise level at which mountains start
    float mountainAmp = 55.0f;       // how tall the peaks can rise
    float mountainScale = 0.005f;    // mountain width
    int mountainOctaves = 4;

    // --- Caves ---
    float caveThreshold = 0.72f; // 3D noise above this -> carved air

    // --- Surface / biomes ---
    int seaLevel = 24;           // water fills up to this y
    int snowHeight = 48;         // terrain at/above this height is snowy
    int beachLevel = 25;         // terrain up to this height is sand

    // --- Trees ---
    int treeChancePct = 5;       // % chance per suitable grass block
    int treeMinHeight = 30;      // trees grow on land within this height band
    int treeMaxHeight = 48;

    // Loads worldgen.conf from the working dir or next to the executable.
    bool loadFile(const char* path);
    void load(const char* exeDirPathBase);
};

// Parses "key=value" lines, skipping blank lines and # comments.
void worldGenApply(WorldGenConfig& c, const std::string& line);
