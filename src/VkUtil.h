#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Core device state shared with chunk mesh uploads.
class VkRenderer;
struct RenderContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkRenderer* owner = nullptr; // used for deferred buffer destruction
};

namespace vkutil {

inline void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] %s failed: %d\n", what, (int)r);
    }
}

inline uint32_t findMemoryType(VkPhysicalDevice phys, uint32_t typeBits,
                               VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    fprintf(stderr, "[vulkan] failed to find memory type\n");
    return 0;
}

inline VkCommandBuffer beginOneShot(const RenderContext& ctx) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = ctx.pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(ctx.device, &ai, &cmd);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

inline void endOneShot(const RenderContext& ctx, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(ctx.queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.queue);
    vkFreeCommandBuffers(ctx.device, ctx.pool, 1, &cmd);
}

// Creates a host-visible, host-coherent buffer and copies data into it.
inline bool createHostBuffer(const RenderContext& ctx, VkDeviceSize size,
                             VkBufferUsageFlags usage,
                             VkBuffer& outBuffer, VkDeviceMemory& outMemory,
                             const void* data = nullptr) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(ctx.device, &bi, nullptr, &outBuffer) != VK_SUCCESS) return false;

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(ctx.device, outBuffer, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(ctx.phys, mr.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(ctx.device, &ai, nullptr, &outMemory) != VK_SUCCESS) return false;
    vkBindBufferMemory(ctx.device, outBuffer, outMemory, 0);

    if (data && size > 0) {
        void* dst = nullptr;
        vkMapMemory(ctx.device, outMemory, 0, size, 0, &dst);
        std::memcpy(dst, data, (size_t)size);
        vkUnmapMemory(ctx.device, outMemory);
    }
    return true;
}

// Creates a device-local buffer and fills it from a host staging buffer.
// Slower to create, but the GPU reads it from VRAM (fast) every frame.
inline bool createDeviceBuffer(const RenderContext& ctx, VkDeviceSize size,
                               VkBufferUsageFlags usage,
                               VkBuffer& outBuffer, VkDeviceMemory& outMemory,
                               const void* data) {
    if (size == 0) {
        outBuffer = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        return true;
    }
    // Staging buffer (host-visible).
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    if (!createHostBuffer(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, stagingMem, data))
        return false;

    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(ctx.device, &bi, nullptr, &outBuffer) != VK_SUCCESS) {
        vkDestroyBuffer(ctx.device, staging, nullptr);
        vkFreeMemory(ctx.device, stagingMem, nullptr);
        return false;
    }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(ctx.device, outBuffer, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(ctx.phys, mr.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(ctx.device, &ai, nullptr, &outMemory) != VK_SUCCESS) {
        vkDestroyBuffer(ctx.device, outBuffer, nullptr);
        vkDestroyBuffer(ctx.device, staging, nullptr);
        vkFreeMemory(ctx.device, stagingMem, nullptr);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(ctx.device, outBuffer, outMemory, 0);

    VkCommandBuffer cmd = beginOneShot(ctx);
    VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd, staging, outBuffer, 1, &region);
    endOneShot(ctx, cmd);

    vkDestroyBuffer(ctx.device, staging, nullptr);
    vkFreeMemory(ctx.device, stagingMem, nullptr);
    return true;
}

// Creates a device-local buffer with no data (used with a batch staging copy).
inline bool createDeviceBufferEmpty(const RenderContext& ctx, VkDeviceSize size,
                                    VkBufferUsageFlags usage,
                                    VkBuffer& outBuffer, VkDeviceMemory& outMemory) {
    if (size == 0) {
        outBuffer = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        return true;
    }
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(ctx.device, &bi, nullptr, &outBuffer) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(ctx.device, outBuffer, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(ctx.phys, mr.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(ctx.device, &ai, nullptr, &outMemory) != VK_SUCCESS) {
        vkDestroyBuffer(ctx.device, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(ctx.device, outBuffer, outMemory, 0);
    return true;
}

// Queues a buffer/memory pair for destruction once the GPU is done with it.
// The renderer drains this list after waiting on its per-frame fence.
void retireImpl(const RenderContext& ctx, VkBuffer buffer, VkDeviceMemory memory);
inline void retire(const RenderContext& ctx, VkBuffer buffer, VkDeviceMemory memory) {
    if (ctx.owner && buffer) retireImpl(ctx, buffer, memory);
}

} // namespace vkutil
