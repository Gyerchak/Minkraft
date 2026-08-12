#include "World.h"
#include "Noise.h"
#include <algorithm>

static const int SEA_LEVEL = 24;

World::World(uint32_t seed) : m_seed(seed) {}

World::~World() = default;

Chunk* World::chunkAt(int cx, int cz) {
    auto it = m_chunks.find(key(cx, cz));
    return it == m_chunks.end() ? nullptr : it->second.get();
}

const Chunk* World::chunkAt(int cx, int cz) const {
    auto it = m_chunks.find(key(cx, cz));
    return it == m_chunks.end() ? nullptr : it->second.get();
}

unsigned char World::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= CH) return AIR;
    int cx = (int)std::floor(x / (float)Chunk::CX);
    int cz = (int)std::floor(z / (float)Chunk::CZ);
    const Chunk* c = chunkAt(cx, cz);
    if (!c) return AIR;
    int lx = x - cx * Chunk::CX;
    int lz = z - cz * Chunk::CZ;
    return c->get(lx, y, lz);
}

void World::setBlock(int x, int y, int z, unsigned char v) {
    if (y < 0 || y >= CH) return;
    int cx = (int)std::floor(x / (float)Chunk::CX);
    int cz = (int)std::floor(z / (float)Chunk::CZ);
    Chunk* c = chunkAt(cx, cz);
    if (!c) return;
    int lx = x - cx * Chunk::CX;
    int lz = z - cz * Chunk::CZ;
    c->set(lx, y, lz, v);
    // Rebuild this chunk and any loaded neighbors that may share the changed face.
    requestMesh(cx, cz);
    requestMesh(cx + 1, cz);
    requestMesh(cx - 1, cz);
    requestMesh(cx, cz + 1);
    requestMesh(cx, cz - 1);
}

float World::terrainHeight(int x, int z) const {
    float n = fbm2D(x * 0.008f, z * 0.008f, m_seed, 4);
    float h = 18.0f + n * 26.0f; // ~[18,44], average ~31 (above sea level)

    // Rolling mountains.
    float m = fbm2D(x * 0.004f + 512.0f, z * 0.004f - 512.0f, m_seed ^ 0x9E3779B9u, 4);
    if (m > 0.55f) {
        h += (m - 0.55f) * 80.0f;
    }
    return h;
}

void World::generateChunk(Chunk& c) {
    const int ox = c.cx * Chunk::CX;
    const int oz = c.cz * Chunk::CZ;
    int heights[Chunk::CX][Chunk::CZ];

    for (int x = 0; x < Chunk::CX; x++) {
        for (int z = 0; z < Chunk::CZ; z++) {
            int wx = ox + x;
            int wz = oz + z;
            int h = (int)std::floor(terrainHeight(wx, wz));
            h = std::clamp(h, 2, Chunk::CY - 8);
            heights[x][z] = h;

            for (int y = 0; y <= h; y++) {
                unsigned char blk;
                if (y == 0) {
                    blk = BEDROCK;
                } else if (y < h - 4) {
                    blk = STONE;
                } else {
                    blk = DIRT;
                }

                // Caves.
                if (blk == STONE && y > 4 && y < h - 1) {
                    float cave = valueNoise3D(wx * 0.05f, y * 0.08f, wz * 0.05f,
                                              m_seed ^ 0xA5A5A5A5u);
                    if (cave > 0.74f) {
                        blk = AIR;
                    }
                }

                if (y == h) {
                    if (h <= SEA_LEVEL + 1) {
                        blk = SAND;
                    } else if (h >= 46) {
                        blk = SNOW;
                    } else {
                        blk = GRASS;
                    }
                }
                c.set(x, y, z, blk);
            }

            // Water fill.
            for (int y = h + 1; y <= SEA_LEVEL && y < Chunk::CY; y++) {
                c.set(x, y, z, WATER);
            }
        }
    }

    // Trees.
    for (int x = 2; x < Chunk::CX - 2; x++) {
        for (int z = 2; z < Chunk::CZ - 2; z++) {
            int wx = ox + x;
            int wz = oz + z;
            int h = heights[x][z];
            if (h < 28 || h >= 46) continue; // only on grass land, not snowy peaks
            if (h <= SEA_LEVEL + 1) continue;
            unsigned char top = c.get(x, h, z);
            if (top != GRASS) continue;
            if (hash3(wx, h, wz, m_seed) % 100 >= 4) continue;

            // Trunk.
            int trunkH = 4 + (int)(hash3(wx, wz, h, m_seed ^ 0x1234u) % 3);
            int topY = h + trunkH;
            if (topY + 2 >= Chunk::CY) continue;
            for (int i = 1; i <= trunkH; i++) {
                if (c.get(x, h + i, z) == AIR) c.set(x, h + i, z, LOG);
            }
            // Canopy.
            for (int dy = 0; dy <= 3; dy++) {
                int r = (dy == 0) ? 1 : ((dy == 1) ? 2 : ((dy == 2) ? 2 : 1));
                int yy = topY + dy;
                if (yy < 0 || yy >= Chunk::CY) continue;
                for (int dx = -r; dx <= r; dx++) {
                    for (int dz = -r; dz <= r; dz++) {
                        if (c.get(x + dx, yy, z + dz) != AIR) continue;
                        if (dx * dx + dz * dz > (r + 1) * (r + 1)) continue;
                        if (dx * dx + dz * dz > r * r && (dy == 3)) continue;
                        if (dy == 0 && std::abs(dx) == r && std::abs(dz) == r) continue;
                        c.set(x + dx, yy, z + dz, LEAVES);
                    }
                }
            }
        }
    }
}

void World::requestMesh(int cx, int cz) {
    if (!chunkAt(cx, cz)) return;
    Key k = key(cx, cz);
    if (m_queued.insert(k).second) {
        m_meshQueue.push_back(k);
    }
}

void World::update(int playerChunkX, int playerChunkZ, int renderDist) {
    // Ensure all chunks in the radius exist (generation happens immediately,
    // meshing happens incrementally via processMeshQueue).
    for (int dz = -renderDist; dz <= renderDist; dz++) {
        for (int dx = -renderDist; dx <= renderDist; dx++) {
            int cx = playerChunkX + dx;
            int cz = playerChunkZ + dz;
            Key k = key(cx, cz);
            auto it = m_chunks.find(k);
            if (it == m_chunks.end()) {
                auto c = std::make_unique<Chunk>(cx, cz);
                generateChunk(*c);
                m_chunks.emplace(k, std::move(c));
                requestMesh(cx, cz);
                // Existing neighbors meshed against AIR need a rebuild too, so
                // border faces stay consistent once this chunk appears.
                requestMesh(cx + 1, cz);
                requestMesh(cx - 1, cz);
                requestMesh(cx, cz + 1);
                requestMesh(cx, cz - 1);
            }
        }
    }

    // Unload far chunks.
    int unloadDist = renderDist + 4;
    for (auto it = m_chunks.begin(); it != m_chunks.end();) {
        int dx = it->second->cx - playerChunkX;
        int dz = it->second->cz - playerChunkZ;
        int d = std::max(std::abs(dx), std::abs(dz));
        if (d > unloadDist) {
            it->second->destroy(*m_ctx);
            it = m_chunks.erase(it);
        } else {
            ++it;
        }
    }
}

void World::clearAll() {
    if (m_ctx) {
        for (auto& [k, c] : m_chunks) c->destroy(*m_ctx);
    }
    m_chunks.clear();
    m_meshQueue.clear();
    m_queued.clear();
}

void World::processMeshQueue() {
    if (!m_ctx) return;
    const int budget = m_meshBudget;

    // Phase 1: CPU rebuild of all pending chunks.
    std::vector<Chunk*> toUpload;
    toUpload.reserve(budget);
    for (int i = 0; i < budget && !m_meshQueue.empty(); i++) {
        Key k = m_meshQueue.front();
        m_meshQueue.pop_front();
        m_queued.erase(k);
        int cx = (int)((uint32_t)(k >> 32));
        int cz = (int)((uint32_t)k);
        Chunk* c = chunkAt(cx, cz);
        if (!c) continue;
        c->rebuild(*this);
        toUpload.push_back(c);
    }
    if (toUpload.empty()) return;

    // Phase 2: batch upload all rebuilt meshes to device-local buffers using
    // one staging buffer + one command buffer + a single queue submit/wait.
    struct Item {
        Chunk* c;
        bool water;
        const std::vector<float>* verts;
        VkBuffer* buf;
        VkDeviceMemory* mem;
        uint32_t* count;
        VkDeviceSize offset;
    };
    std::vector<Item> items;
    VkDeviceSize total = 0;
    for (Chunk* c : toUpload) {
        // Retire old GPU buffers (deferred destroy once the GPU is done).
        vkutil::retire(*m_ctx, c->solidBuf, c->solidMem);
        vkutil::retire(*m_ctx, c->waterBuf, c->waterMem);
        c->solidBuf = c->waterBuf = VK_NULL_HANDLE;
        c->solidMem = c->waterMem = VK_NULL_HANDLE;
        c->solidCount_ = c->waterCount_ = 0;
        if (!c->solidVerts_.empty()) {
            items.push_back({c, false, &c->solidVerts_, &c->solidBuf, &c->solidMem, &c->solidCount_, 0});
            total += c->solidVerts_.size() * sizeof(float);
        }
        if (!c->waterVerts_.empty()) {
            items.push_back({c, true, &c->waterVerts_, &c->waterBuf, &c->waterMem, &c->waterCount_, 0});
            total += c->waterVerts_.size() * sizeof(float);
        }
    }
    if (items.empty()) return;

    // One staging buffer containing every mesh.
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    if (!vkutil::createHostBuffer(*m_ctx, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  staging, stagingMem)) {
        return;
    }
    void* map = nullptr;
    vkMapMemory(m_ctx->device, stagingMem, 0, total, 0, &map);
    VkDeviceSize offset = 0;
    for (Item& it : items) {
        std::memcpy((char*)map + offset, it.verts->data(), it.verts->size() * sizeof(float));
        it.offset = offset;
        offset += it.verts->size() * sizeof(float);
    }
    vkUnmapMemory(m_ctx->device, stagingMem);

    // Create the device-local buffers and record one copy each.
    std::vector<VkBuffer> created;
    bool ok = true;
    for (Item& it : items) {
        VkDeviceSize sz = it.verts->size() * sizeof(float);
        if (!vkutil::createDeviceBufferEmpty(*m_ctx, sz, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                             *it.buf, *it.mem)) {
            ok = false;
            break;
        }
        created.push_back(*it.buf);
    }
    if (ok) {
        VkCommandBuffer cmd = vkutil::beginOneShot(*m_ctx);
        for (Item& it : items) {
            VkBufferCopy region{it.offset, 0, it.verts->size() * sizeof(float)};
            vkCmdCopyBuffer(cmd, staging, *it.buf, 1, &region);
        }
        vkutil::endOneShot(*m_ctx, cmd);
        for (Item& it : items)
            *it.count = (uint32_t)(it.verts->size() / 8);
    }

    vkDestroyBuffer(m_ctx->device, staging, nullptr);
    vkFreeMemory(m_ctx->device, stagingMem, nullptr);
}
