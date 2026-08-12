#pragma once
#include <unordered_map>
#include <memory>
#include <deque>
#include <set>
#include <functional>
#include <cstdint>
#include <cmath>
#include "Chunk.h"
#include "VkUtil.h"
#include "WorldGenConfig.h"

class World {
public:
    World(uint32_t seed, const WorldGenConfig& cfg);
    ~World();

    // Keep chunks within renderDist of the player loaded & meshed.
    void update(int playerChunkX, int playerChunkZ, int renderDist);

    unsigned char getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, unsigned char v);

    bool isSolid(int x, int y, int z) const {
        unsigned char b = getBlock(x, y, z);
        return blockDef(b).solid;
    }

    // Called each frame to mesh a limited number of pending chunks.
    void processMeshQueue();
    void setMeshBudget(int b) { m_meshBudget = b; }
    bool meshQueueEmpty() const { return m_meshQueue.empty(); }

    // Frees all chunk GPU buffers (call before the renderer shuts down).
    void clearAll();

    void setRenderContext(const RenderContext* ctx) { m_ctx = ctx; }

    void forEachChunk(const std::function<void(const Chunk&)>& f) const {
        for (auto& [k, c] : m_chunks) {
            (void)k;
            f(*c);
        }
    }

    uint32_t seed() const { return m_seed; }
    size_t chunkCount() const { return m_chunks.size(); }
    int surfaceHeight(int x, int z) const { return (int)std::floor(terrainHeight(x, z)); }
    static const int CH = Chunk::CY;

private:
    uint32_t m_seed;
    WorldGenConfig m_cfg;
    const RenderContext* m_ctx = nullptr;
    int m_meshBudget = 32;
    using Key = int64_t;
    static Key key(int cx, int cz) { return ((int64_t)(uint32_t)cx << 32) | (uint32_t)cz; }

    std::unordered_map<Key, std::unique_ptr<Chunk>> m_chunks;
    std::deque<Key> m_meshQueue;
    std::set<Key> m_queued;

    Chunk* chunkAt(int cx, int cz);
    const Chunk* chunkAt(int cx, int cz) const;

    void generateChunk(Chunk& c);
    void requestMesh(int cx, int cz);
    float terrainHeight(int x, int z) const;
};
