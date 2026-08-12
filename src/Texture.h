#pragma once
#include <cstdint>
#include <vector>

// CPU-side texture atlas for the block texture pack.
// Textures are loaded from PNG files (16x16 per block) in a textures/ folder.
// Each tile is embedded in a 24x24 padded region (4px of edge texels on every
// side) so the GPU can use LINEAR + anisotropic filtering without color
// bleeding between tiles. Mipmaps are generated per tile (padding included).
class TextureAtlas {
public:
    static const int NUM_TILES = 21; // used tile slots 0..20
    static const int TILE = 16;      // content pixels per tile
    static const int PAD = 4;        // padding texels per side (>= mip filter radius)
    static const int PADDED = TILE + 2 * PAD; // 24
    static const int GRID = 5;       // tiles per side
    static const int SIZE = GRID * PADDED;    // 120
    static const int MAX_LEVEL = 3;  // atlas sizes: 120, 60, 30, 15

    struct Level {
        int w = 0;
        std::vector<unsigned char> px; // RGBA, w*w*4 bytes
    };

    TextureAtlas() = default;

    // Loads textures/<name>.png for each tile. Missing/invalid files fall back
    // to the built-in procedural artwork. texturesDir may be "" (fallback only).
    void generate(const char* texturesDir);

    const std::vector<Level>& levels() const { return m_levels; }

    // Filename stem (without .png) for a tile slot.
    static const char* tileName(int tile);

    // Paints the built-in 16x16 RGBA content for a tile slot (fallback art).
    static void paintTileContent(int tile, unsigned char* out);

    // UVs of a tile's unpadded content region (Vulkan v convention: v=0 = top).
    static void tileUV(int tile, float& u0, float& v0, float& u1, float& v1) {
        int col = tile % GRID;
        int row = tile / GRID;
        float w = (float)SIZE;
        u0 = (col * PADDED + PAD) / w;
        u1 = (col * PADDED + PAD + TILE) / w;
        v1 = (row * PADDED + PAD) / w;
        v0 = (row * PADDED + PAD + TILE) / w;
    }

private:
    std::vector<Level> m_levels;

    void setTile(int col, int row, const unsigned char* pixels);
    static void resampleNearest(const unsigned char* src, int sw, int sh,
                                unsigned char* dst, int dw, int dh);

    static void paintGrassTop(unsigned char* p, int s);
    static void paintGrassSide(unsigned char* p, int s);
    static void paintDirt(unsigned char* p, int s);
    static void paintStone(unsigned char* p, int s);
    static void paintSand(unsigned char* p, int s);
    static void paintWater(unsigned char* p, int s);
    static void paintLogSide(unsigned char* p, int s);
    static void paintLogTop(unsigned char* p, int s);
    static void paintLeaves(unsigned char* p, int s);
    static void paintSnow(unsigned char* p, int s);
    static void paintPlanks(unsigned char* p, int s);
    static void paintBedrock(unsigned char* p, int s);
    static void paintGrassSnowSide(unsigned char* p, int s);
    static void paintCrack(unsigned char* p, int s, int stage);
};
