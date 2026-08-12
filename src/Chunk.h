#pragma once
#include <vector>
#include "Block.h"
#include "VkUtil.h"

class World;

class Chunk {
public:
    static const int CX = 16;
    static const int CY = 64;
    static const int CZ = 16;

    friend class World;

    int cx, cz;
    unsigned char blocks[CX * CY * CZ];

    Chunk(int cx, int cz);
    ~Chunk();

    unsigned char get(int x, int y, int z) const {
        return blocks[(y * CZ + z) * CX + x];
    }
    void set(int x, int y, int z, unsigned char v) {
        blocks[(y * CZ + z) * CX + x] = v;
    }

    // CPU-only: rebuild the triangle meshes (solid + water) from the block
    // data. GPU upload happens separately via uploadToGPU().
    void rebuild(World& world);

    // Uploads the CPU mesh to a fresh device-local vertex buffer.
    void uploadToGPU(const RenderContext& ctx);

    // Frees GPU resources (deferred so the GPU is done with them).
    void destroy(const RenderContext& ctx);

    VkBuffer solidBuffer() const { return solidBuf; }
    VkBuffer waterBuffer() const { return waterBuf; }
    uint32_t solidCount() const { return solidCount_; }
    uint32_t waterCount() const { return waterCount_; }
    bool hasWater() const { return waterCount_ > 0; }

    const std::vector<float>& solidVerts() const { return solidVerts_; }
    const std::vector<float>& waterVerts() const { return waterVerts_; }

private:
    std::vector<float> solidVerts_, waterVerts_;
    VkBuffer solidBuf = VK_NULL_HANDLE;
    VkDeviceMemory solidMem = VK_NULL_HANDLE;
    VkBuffer waterBuf = VK_NULL_HANDLE;
    VkDeviceMemory waterMem = VK_NULL_HANDLE;
    uint32_t solidCount_ = 0;
    uint32_t waterCount_ = 0;
};
