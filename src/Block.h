#pragma once

// Block ids.
enum BlockId : unsigned char {
    AIR = 0,
    GRASS,
    DIRT,
    STONE,
    SAND,
    WATER,
    LOG,
    LEAVES,
    SNOW,
    PLANKS,
    BEDROCK,
    NUM_BLOCKS,
};

// Face indices.
enum Face {
    FACE_PX = 0,
    FACE_NX = 1,
    FACE_PY = 2, // top
    FACE_NY = 3, // bottom
    FACE_PZ = 4,
    FACE_NZ = 5,
    NUM_FACES = 6,
};

// Tile indices in the 4x4 texture atlas (row-major, row 0 = top of atlas).
enum Tile {
    TILE_GRASS_TOP = 0,
    TILE_GRASS_SIDE = 1,
    TILE_DIRT = 2,
    TILE_STONE = 3,
    TILE_SAND = 4,
    TILE_WATER = 5,
    TILE_LOG_SIDE = 6,
    TILE_LOG_TOP = 7,
    TILE_LEAVES = 8,
    TILE_SNOW = 9,
    TILE_PLANKS = 10,
    TILE_BEDROCK = 11,
    TILE_GRASS_SNOW_SIDE = 12,
};

struct BlockDef {
    bool solid;       // collides with player
    bool transparent; // never causes adjacent solid faces to be hidden
    bool water;
    const char* name;
};

inline const BlockDef& blockDef(unsigned char id) {
    static const BlockDef defs[NUM_BLOCKS] = {
        {false, true,  false, "Air"},
        {true,  false, false, "Grass"},
        {true,  false, false, "Dirt"},
        {true,  false, false, "Stone"},
        {true,  false, false, "Sand"},
        {false, true,  true,  "Water"},
        {true,  false, false, "Log"},
        {false, true,  false, "Leaves"},
        {true,  false, false, "Snow"},
        {true,  false, false, "Planks"},
        {true,  false, false, "Bedrock"},
    };
    return defs[id < NUM_BLOCKS ? id : 0];
}

inline int tileFor(unsigned char id, int face) {
    switch (id) {
        case GRASS:
            if (face == FACE_PY) return TILE_GRASS_TOP;
            if (face == FACE_NY) return TILE_DIRT;
            return TILE_GRASS_SIDE;
        case DIRT: return TILE_DIRT;
        case STONE: return TILE_STONE;
        case SAND: return TILE_SAND;
        case WATER: return TILE_WATER;
        case LOG:
            if (face == FACE_PY || face == FACE_NY) return TILE_LOG_TOP;
            return TILE_LOG_SIDE;
        case LEAVES: return TILE_LEAVES;
        case SNOW:
            if (face == FACE_PY) return TILE_SNOW;
            return TILE_GRASS_SNOW_SIDE;
        case PLANKS: return TILE_PLANKS;
        case BEDROCK: return TILE_BEDROCK;
        default: return TILE_STONE;
    }
}
