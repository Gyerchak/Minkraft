#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <functional>
#include "Math.h"
#include "VkUtil.h"
#include "Shader.h"
#include "Texture.h"

class World;

// Everything the renderer needs to draw one frame.
struct FrameState {
    Mat4 viewProj;
    Vec3 camPos;
    Vec3 fogColor;
    float fogNear = 20.0f;
    float fogFar = 110.0f;
    bool showBox = false;
    Vec3 boxPos;
};

class VkRenderer {
public:
    bool init(GLFWwindow* window, const TextureAtlas& atlas);
    void shutdown();

    void drawFrame(World& world, const FrameState& fs);

    const RenderContext& ctx() const { return m_ctx; }
    bool valid() const { return m_swapchainValid; }
    void retireBuffer(VkBuffer b, VkDeviceMemory m);

    // Sets the texture anisotropy level (1,2,4,8,16) used by the atlas sampler.
    void setAnisotropy(int a) { m_anisotropy = a > 0 ? a : 1; }

    // Captures the next presented frame to a 24-bit BMP file.
    void requestScreenshot();
    bool saveScreenshot(const char* path);

private:
    static const int ATLAS_LEVELS = TextureAtlas::MAX_LEVEL + 1;

    RenderContext m_ctx;
    GLFWwindow* m_window = nullptr;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    int m_anisotropy = 8;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_views;
    std::vector<VkFramebuffer> m_framebuffers;
    std::vector<VkSemaphore> m_acquireSemaphores;

    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMem = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;

    VkRenderPass m_renderPass = VK_NULL_HANDLE;

    VkImage m_atlasImage = VK_NULL_HANDLE;
    VkDeviceMemory m_atlasMem = VK_NULL_HANDLE;
    VkImageView m_atlasView = VK_NULL_HANDLE;
    VkSampler m_atlasSampler = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_worldSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_boxSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_crossSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;

    VkPipelineLayout m_worldLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_boxLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_crossLayout = VK_NULL_HANDLE;
    VkPipeline m_worldSolid = VK_NULL_HANDLE;
    VkPipeline m_worldWater = VK_NULL_HANDLE;
    VkPipeline m_box = VK_NULL_HANDLE;
    VkPipeline m_cross = VK_NULL_HANDLE;

    VkBuffer m_boxBuf = VK_NULL_HANDLE;
    VkDeviceMemory m_boxMem = VK_NULL_HANDLE;
    VkBuffer m_crossBuf = VK_NULL_HANDLE;
    VkDeviceMemory m_crossMem = VK_NULL_HANDLE;
    VkBuffer m_shotBuf = VK_NULL_HANDLE;
    VkDeviceMemory m_shotMem = VK_NULL_HANDLE;
    VkDeviceSize m_shotSize = 0;
    bool m_doScreenshot = false;

    struct Frame {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
        VkBuffer ubo = VK_NULL_HANDLE;
        VkDeviceMemory uboMem = VK_NULL_HANDLE;
        VkDescriptorSet worldSet = VK_NULL_HANDLE;
        VkDescriptorSet boxSet = VK_NULL_HANDLE;
        void* uboMap = nullptr;
        std::vector<std::pair<VkBuffer, VkDeviceMemory>> pendingDestroy;
    };
    std::vector<Frame> m_frames;
    uint32_t m_frameIndex = 0;
    uint32_t m_imageIndex = 0;
    bool m_swapchainValid = false;

    struct UboData {
        Mat4 viewProj;    // offset 0
        float camPos[4];  // offset 64
        float fogColor[4];// offset 80
        float fogParams[4];// offset 96
    };

    VkSurfaceFormatKHR pickFormat(const std::vector<VkSurfaceFormatKHR>&) const;
    VkPresentModeKHR pickPresentMode(const std::vector<VkPresentModeKHR>&) const;
    VkExtent2D chooseExtent(GLFWwindow*, const VkSurfaceCapabilitiesKHR&) const;
    uint32_t findQueueFamily() const;
    bool createInstance(const char* appName);
    bool createSurface(GLFWwindow*);
    bool createDevice();
    bool createSwapchain(GLFWwindow*);
    void destroySwapchain();
    bool createDepthResources();
    bool createRenderPass();
    bool createPipelines();
    bool createAtlas(const TextureAtlas&);
    bool createDescriptors();
    bool createUiBuffers();
    void createFrameResources();
    void updateUbo(Frame& f, const FrameState& fs);
    void recordFrame(Frame& f, World& world, const FrameState& fs);
    void recreateSwapchain(GLFWwindow*);
    void destroyPipelines();
};
