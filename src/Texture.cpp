#include "Texture.h"
#include "PngLoader.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <algorithm>

namespace {
inline void px(unsigned char* p, int s, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
    unsigned char* o = p + (y * s + x) * 4;
    o[0] = r; o[1] = g; o[2] = b; o[3] = a;
}
// Fill the whole tile with one solid color.
inline void flatFill(unsigned char* p, int s, int rgb[3], int a = 255) {
    for (int y = 0; y < TextureAtlas::TILE; y++)
        for (int x = 0; x < TextureAtlas::TILE; x++)
            px(p, s, x, y, rgb[0], rgb[1], rgb[2], a);
}
}

void TextureAtlas::setTile(int col, int row, const unsigned char* tile) {
    // Blit the 16x16 content tile into the padded level-0 atlas and extend the
    // edges into the padding.
    Level& l0 = m_levels[0];
    const int ox = col * PADDED;
    const int oy = row * PADDED;
    for (int y = 0; y < PADDED; y++) {
        int sy = y - PAD;
        sy = sy < 0 ? 0 : (sy >= TILE ? TILE - 1 : sy);
        for (int x = 0; x < PADDED; x++) {
            int sx = x - PAD;
            sx = sx < 0 ? 0 : (sx >= TILE ? TILE - 1 : sx);
            const unsigned char* src = tile + (sy * TILE + sx) * 4;
            unsigned char* dst = l0.px.data() + ((oy + y) * SIZE + (ox + x)) * 4;
            std::memcpy(dst, src, 4);
        }
    }
}

void TextureAtlas::resampleNearest(const unsigned char* src, int sw, int sh,
                                   unsigned char* dst, int dw, int dh) {
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++) {
            int sx = std::min(sw - 1, x * sw / dw);
            int sy = std::min(sh - 1, y * sh / dh);
            std::memcpy(dst + (y * dw + x) * 4, src + (sy * sw + sx) * 4, 4);
        }
    }
}

const char* TextureAtlas::tileName(int tile) {
    static const char* const names[NUM_TILES] = {
        "grass_top", "grass_side", "dirt", "stone", "sand", "water",
        "log_side", "log_top", "leaves", "snow", "planks", "bedrock",
        "grass_snow_side",
    };
    return (tile >= 0 && tile < NUM_TILES) ? names[tile] : "";
}

void TextureAtlas::paintTileContent(int tile, unsigned char* out) {
    switch (tile) {
        case 0: paintGrassTop(out, TILE); break;
        case 1: paintGrassSide(out, TILE); break;
        case 2: paintDirt(out, TILE); break;
        case 3: paintStone(out, TILE); break;
        case 4: paintSand(out, TILE); break;
        case 5: paintWater(out, TILE); break;
        case 6: paintLogSide(out, TILE); break;
        case 7: paintLogTop(out, TILE); break;
        case 8: paintLeaves(out, TILE); break;
        case 9: paintSnow(out, TILE); break;
        case 10: paintPlanks(out, TILE); break;
        case 11: paintBedrock(out, TILE); break;
        case 12: paintGrassSnowSide(out, TILE); break;
        default: std::memset(out, 0, (size_t)TILE * TILE * 4); break;
    }
}

void TextureAtlas::generate(const char* texturesDir) {
    m_levels.resize(MAX_LEVEL + 1);
    m_levels[0].w = SIZE;
    m_levels[0].px.assign((size_t)SIZE * SIZE * 4, 0);

    // Load each tile: PNG from the texture pack if available, else procedural.
    for (int t = 0; t < NUM_TILES; t++) {
        unsigned char tile[TILE * TILE * 4];
        std::memset(tile, 0, sizeof(tile));
        bool loaded = false;
        if (texturesDir && *texturesDir) {
            std::string path = std::string(texturesDir) + "/" + tileName(t) + ".png";
            std::vector<unsigned char> img;
            int w = 0, h = 0;
            if (loadPng(path.c_str(), img, w, h) && w > 0 && h > 0) {
                resampleNearest(img.data(), w, h, tile, TILE, TILE);
                loaded = true;
            }
        }
        if (!loaded) paintTileContent(t, tile);
        setTile(t % GRID, t / GRID, tile);
    }
    // remaining tiles stay transparent (never sampled)

    // Downsample per tile (padding included) so mips never mix tiles.
    for (int level = 1; level <= MAX_LEVEL; level++) {
        int w = m_levels[level - 1].w;
        int nw = w / 2;
        int padSz = PADDED >> level; // padded tile size at this level
        m_levels[level].w = nw;
        m_levels[level].px.assign((size_t)nw * nw * 4, 0);
        for (int t = 0; t < GRID * GRID; t++) {
            int tcol = t % GRID;
            int trow = t / GRID;
            for (int y = 0; y < padSz; y++) {
                for (int x = 0; x < padSz; x++) {
                    int sx = tcol * padSz * 2 + x * 2;
                    int sy = trow * padSz * 2 + y * 2;
                    unsigned c[4] = {0, 0, 0, 0};
                    for (int yy = 0; yy < 2; yy++)
                        for (int xx = 0; xx < 2; xx++) {
                            const unsigned char* p =
                                m_levels[level - 1].px.data() + ((sy + yy) * w + (sx + xx)) * 4;
                            for (int k = 0; k < 4; k++) c[k] += p[k];
                        }
                    unsigned char* o = m_levels[level].px.data() +
                                       ((trow * padSz + y) * nw + (tcol * padSz + x)) * 4;
                    for (int k = 0; k < 4; k++) o[k] = (unsigned char)(c[k] / 4);
                }
            }
        }
    }
}

void TextureAtlas::paintGrassTop(unsigned char* p, int s) {
    int rgb[3] = {92, 152, 64};
    flatFill(p, s, rgb);
}

void TextureAtlas::paintGrassSide(unsigned char* p, int s) {
    int rgb[3] = {124, 94, 66};
    flatFill(p, s, rgb);
    // Green turf cap.
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < TILE; x++)
            px(p, s, x, y, 92, 152, 64);
    for (int x = 0; x < TILE; x++)
        px(p, s, x, 3, 84, 128, 58);
}

void TextureAtlas::paintDirt(unsigned char* p, int s) {
    int rgb[3] = {134, 96, 67};
    flatFill(p, s, rgb);
}

void TextureAtlas::paintStone(unsigned char* p, int s) {
    int rgb[3] = {127, 127, 127};
    flatFill(p, s, rgb);
}

void TextureAtlas::paintSand(unsigned char* p, int s) {
    int rgb[3] = {219, 206, 164};
    flatFill(p, s, rgb);
}

void TextureAtlas::paintWater(unsigned char* p, int s) {
    int rgb[3] = {56, 100, 188};
    flatFill(p, s, rgb, 245);
}

void TextureAtlas::paintLogSide(unsigned char* p, int s) {
    int rgb[3] = {104, 82, 50};
    flatFill(p, s, rgb);
}

void TextureAtlas::paintLogTop(unsigned char* p, int s) {
    int rgb[3] = {150, 118, 74};
    flatFill(p, s, rgb);
}

void TextureAtlas::paintLeaves(unsigned char* p, int s) {
    int rgb[3] = {52, 108, 36};
    flatFill(p, s, rgb);
}

void TextureAtlas::paintSnow(unsigned char* p, int s) {
    int rgb[3] = {240, 246, 250};
    flatFill(p, s, rgb);
}

void TextureAtlas::paintPlanks(unsigned char* p, int s) {
    int rgb[3] = {162, 126, 78};
    flatFill(p, s, rgb);
}

void TextureAtlas::paintBedrock(unsigned char* p, int s) {
    int rgb[3] = {76, 76, 76};
    flatFill(p, s, rgb);
}

void TextureAtlas::paintGrassSnowSide(unsigned char* p, int s) {
    int rgb[3] = {124, 94, 66};
    flatFill(p, s, rgb);
    for (int y = 0; y < 7; y++)
        for (int x = 0; x < TILE; x++)
            px(p, s, x, y, 240, 246, 250);
}
