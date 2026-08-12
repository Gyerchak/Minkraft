#include "Chunk.h"
#include "World.h"
#include "Texture.h"
#include <cstring>
#include <algorithm>

// Per-face: 4 corners (bottom-left, bottom-right, top-right, top-left).
// Winding is CCW when viewed from outside (outward normal).
static const float FACE_DATA[6][4][3] = {
    // +X
    {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}},
    // -X
    {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
    // +Y (top)
    {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},
    // -Y (bottom)
    {{1, 0, 0}, {1, 0, 1}, {0, 0, 1}, {0, 0, 0}},
    // +Z
    {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}},
    // -Z
    {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},
};

static const float FACE_NORMAL[6][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

static const int DIR[6][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

static inline bool faceVisible(unsigned char cur, unsigned char neighbor) {
    if (neighbor == AIR) return true;
    if (cur == WATER) return neighbor != WATER;
    if (neighbor == WATER) return true;
    if (neighbor == LEAVES) return cur != LEAVES;
    return false;
}

Chunk::Chunk(int cx, int cz) : cx(cx), cz(cz) {
    std::memset(blocks, 0, sizeof(blocks));
}

Chunk::~Chunk() = default;

void Chunk::rebuild(World& world) {
    solidVerts_.clear();
    waterVerts_.clear();
    solidVerts_.reserve(64 * 1024);
    waterVerts_.reserve(16 * 1024);

    for (int y = 0; y < CY; y++) {
        for (int z = 0; z < CZ; z++) {
            for (int x = 0; x < CX; x++) {
                unsigned char b = get(x, y, z);
                if (b == AIR) continue;

                int wx = cx * CX + x;
                int wz = cz * CZ + z;
                bool isWater = blockDef(b).water;
                std::vector<float>& out = isWater ? waterVerts_ : solidVerts_;

                for (int f = 0; f < NUM_FACES; f++) {
                    // Neighbor block: fast local array access when inside the
                    // chunk, world lookup only for chunk-border faces.
                    int nx = x + DIR[f][0];
                    int ny = y + DIR[f][1];
                    int nz = z + DIR[f][2];
                    unsigned char nb;
                    if (nx >= 0 && nx < CX && ny >= 0 && ny < CY && nz >= 0 && nz < CZ)
                        nb = get(nx, ny, nz);
                    else
                        nb = world.getBlock(wx + DIR[f][0], y + DIR[f][1], wz + DIR[f][2]);
                    if (!faceVisible(b, nb)) continue;

                    float u0, v0, u1, v1;
                    TextureAtlas::tileUV(tileFor(b, f), u0, v0, u1, v1);
                    static const float UVS[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

                    // A quad as two triangles (TRIANGLE_LIST needs 6 vertices).
                    // Using 4 vertices here would make the GPU stitch garbage
                    // triangles between consecutive faces -> shattered cubes.
                    static const int TRIS[6] = {0, 1, 2, 0, 2, 3};
                    for (int c = 0; c < 6; c++) {
                        const float* corner = FACE_DATA[f][TRIS[c]];
                        float u = u0 + (u1 - u0) * UVS[TRIS[c]][0];
                        float v = v0 + (v1 - v0) * UVS[TRIS[c]][1];
                        out.push_back(wx + corner[0]);
                        out.push_back(y + corner[1]);
                        out.push_back(wz + corner[2]);
                        out.push_back(FACE_NORMAL[f][0]);
                        out.push_back(FACE_NORMAL[f][1]);
                        out.push_back(FACE_NORMAL[f][2]);
                        out.push_back(u);
                        out.push_back(v);
                    }
                }
            }
        }
    }
}

void Chunk::uploadToGPU(const RenderContext& ctx) {
    // Retire old buffers (deferred destroy after the GPU is done with them).
    vkutil::retire(ctx, solidBuf, solidMem);
    vkutil::retire(ctx, waterBuf, waterMem);
    solidBuf = waterBuf = VK_NULL_HANDLE;
    solidMem = waterMem = VK_NULL_HANDLE;

    auto upload = [&](const std::vector<float>& verts, VkBuffer& buf,
                      VkDeviceMemory& mem, uint32_t& count) {
        if (verts.empty()) {
            count = 0;
            return;
        }
        count = (uint32_t)(verts.size() / 8);
        if (!vkutil::createDeviceBuffer(ctx, verts.size() * sizeof(float),
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                        buf, mem, verts.data())) {
            fprintf(stderr, "[chunk] failed to create vertex buffer\n");
            count = 0;
        }
    };
    upload(solidVerts_, solidBuf, solidMem, solidCount_);
    upload(waterVerts_, waterBuf, waterMem, waterCount_);
}

void Chunk::destroy(const RenderContext& ctx) {
    vkutil::retire(ctx, solidBuf, solidMem);
    vkutil::retire(ctx, waterBuf, waterMem);
    solidBuf = waterBuf = VK_NULL_HANDLE;
    solidMem = waterMem = VK_NULL_HANDLE;
    solidCount_ = waterCount_ = 0;
}
