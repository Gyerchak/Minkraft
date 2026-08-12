#include "VkRenderer.h"
#include "World.h"
#include "Chunk.h"
#include <cstring>
#include <algorithm>

namespace {

VkPipelineShaderStageCreateInfo stageInfo(VkShaderStageFlagBits stage, VkShaderModule mod) {
    VkPipelineShaderStageCreateInfo si{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    si.stage = stage;
    si.module = mod;
    si.pName = "main";
    return si;
}

// View frustum extracted from the view-projection matrix (Vulkan 0..1 depth).
struct Frustum {
    float p[6][4]; // planes (a,b,c,d); inside if a*x+b*y+c*z+d >= 0

    void extract(const Mat4& m) {
        const float* M = m.m;
        // Column-major rows.
        float r0[4] = {M[0], M[4], M[8],  M[12]};
        float r1[4] = {M[1], M[5], M[9],  M[13]};
        float r2[4] = {M[2], M[6], M[10], M[14]};
        float r3[4] = {M[3], M[7], M[11], M[15]};
        for (int i = 0; i < 4; i++) {
            p[0][i] = r3[i] + r0[i]; // left   (x+w >= 0)
            p[1][i] = r3[i] - r0[i]; // right  (w-x >= 0)
            p[2][i] = r3[i] + r1[i]; // bottom (y+w >= 0)
            p[3][i] = r3[i] - r1[i]; // top    (w-y >= 0)
            p[4][i] = r2[i];         // near   (z >= 0)
            p[5][i] = r3[i] - r2[i]; // far    (z <= w)
        }
    }

    bool visible(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) const {
        for (int i = 0; i < 6; i++) {
            float nx = p[i][0], ny = p[i][1], nz = p[i][2];
            // AABB is fully outside this plane when its most-positive corner is outside.
            float px = nx >= 0 ? maxX : minX;
            float py = ny >= 0 ? maxY : minY;
            float pz = nz >= 0 ? maxZ : minZ;
            if (nx * px + ny * py + nz * pz + p[i][3] < 0) return false;
        }
        return true;
    }
};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    (void)type; (void)user;
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        fprintf(stderr, "[vulkan validation] %s\n", data->pMessage);
    return VK_FALSE;
}

} // namespace

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
bool VkRenderer::createInstance(const char* appName) {
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.pApplicationName = appName;
    ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.pEngineName = "Minkraft";
    ai.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.apiVersion = VK_API_VERSION_1_2;

    uint32_t extCount = 0;
    const char** ext = glfwGetRequiredInstanceExtensions(&extCount);
    if (!ext || extCount == 0) {
        fprintf(stderr, "[vulkan] GLFW did not report instance extensions\n");
        return false;
    }
    std::vector<const char*> extensions(ext, ext + extCount);

    // Try to enable validation + debug utils for development.
    bool haveValidation = false;
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
    for (auto& l : layers) {
        if (strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) haveValidation = true;
    }
    bool haveDebugUtils = false;
    uint32_t instExtCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &instExtCount, nullptr);
    std::vector<VkExtensionProperties> instExt(instExtCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &instExtCount, instExt.data());
    for (auto& e : instExt) {
        if (strcmp(e.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) haveDebugUtils = true;
    }

    const char* layerName = "VK_LAYER_KHRONOS_validation";
    std::vector<const char*> enabledLayers;
    if (haveValidation) {
        enabledLayers.push_back(layerName);
        if (haveDebugUtils) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &ai;
    ci.enabledExtensionCount = (uint32_t)extensions.size();
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = (uint32_t)enabledLayers.size();
    ci.ppEnabledLayerNames = enabledLayers.data();
    if (vkCreateInstance(&ci, nullptr, &m_ctx.instance) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create instance\n");
        return false;
    }

    if (haveValidation && haveDebugUtils) {
        auto createMsg = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_ctx.instance, "vkCreateDebugUtilsMessengerEXT");
        if (createMsg) {
            VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
            mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mi.pfnUserCallback = debugCallback;
            createMsg(m_ctx.instance, &mi, nullptr, &m_debugMessenger);
        }
    }
    return true;
}

bool VkRenderer::createSurface(GLFWwindow* window) {
    return glfwCreateWindowSurface(m_ctx.instance, window, nullptr, &m_surface) == VK_SUCCESS;
}

uint32_t VkRenderer::findQueueFamily() const {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_ctx.phys, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(m_ctx.phys, &count, props.data());
    for (uint32_t i = 0; i < count; i++) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(m_ctx.phys, i, m_surface, &present);
            if (present) return i;
        }
    }
    return UINT32_MAX;
}

bool VkRenderer::createDevice() {
    m_ctx.phys = VK_NULL_HANDLE;
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_ctx.instance, &count, nullptr);
    if (count == 0) {
        fprintf(stderr, "[vulkan] no physical devices\n");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_ctx.instance, &count, devices.data());
    for (auto d : devices) {
        m_ctx.phys = d;
        uint32_t fam = findQueueFamily();
        if (fam != UINT32_MAX) {
            m_ctx.queueFamily = fam;
            break;
        }
        m_ctx.phys = VK_NULL_HANDLE;
    }
    if (!m_ctx.phys) {
        fprintf(stderr, "[vulkan] no suitable physical device\n");
        return false;
    }

    VkPhysicalDeviceFeatures feats{};
    feats.samplerAnisotropy = VK_TRUE;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = m_ctx.queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    const char* devExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExt;
    dci.pEnabledFeatures = &feats;

    if (vkCreateDevice(m_ctx.phys, &dci, nullptr, &m_ctx.device) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create device\n");
        return false;
    }
    vkGetDeviceQueue(m_ctx.device, m_ctx.queueFamily, 0, &m_ctx.queue);

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = m_ctx.queueFamily;
    if (vkCreateCommandPool(m_ctx.device, &pci, nullptr, &m_ctx.pool) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create command pool\n");
        return false;
    }
    return true;
}

VkSurfaceFormatKHR VkRenderer::pickFormat(const std::vector<VkSurfaceFormatKHR>& fmts) const {
    for (auto& f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    return fmts[0];
}

VkPresentModeKHR VkRenderer::pickPresentMode(const std::vector<VkPresentModeKHR>& modes) const {
    for (auto m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    for (auto m : modes)
        if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VkRenderer::chooseExtent(GLFWwindow* w, const VkSurfaceCapabilitiesKHR& caps) const {
    if (caps.currentExtent.width != UINT32_MAX) return caps.currentExtent;
    int width, height;
    glfwGetFramebufferSize(w, &width, &height);
    VkExtent2D e{(uint32_t)width, (uint32_t)height};
    e.width = std::max(caps.minImageExtent.width, std::min(caps.maxImageExtent.width, e.width));
    e.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, e.height));
    return e;
}

bool VkRenderer::createSwapchain(GLFWwindow* window) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_ctx.phys, m_surface, &caps);
    uint32_t fmtCount = 0, pmCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_ctx.phys, m_surface, &fmtCount, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_ctx.phys, m_surface, &pmCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    std::vector<VkPresentModeKHR> modes(pmCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_ctx.phys, m_surface, &fmtCount, fmts.data());
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_ctx.phys, m_surface, &pmCount, modes.data());

    VkSurfaceFormatKHR fmt = pickFormat(fmts);
    m_format = fmt.format;
    m_extent = chooseExtent(window, caps);

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = m_surface;
    sci.minImageCount = imageCount;
    sci.imageFormat = fmt.format;
    sci.imageColorSpace = fmt.colorSpace;
    sci.imageExtent = m_extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = pickPresentMode(modes);
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_ctx.device, &sci, nullptr, &m_swapchain) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] failed to create swapchain\n");
        return false;
    }
    uint32_t imgCount = 0;
    vkGetSwapchainImagesKHR(m_ctx.device, m_swapchain, &imgCount, nullptr);
    m_images.resize(imgCount);
    vkGetSwapchainImagesKHR(m_ctx.device, m_swapchain, &imgCount, m_images.data());

    m_views.resize(imgCount);
    for (uint32_t i = 0; i < imgCount; i++) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = m_images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = m_format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(m_ctx.device, &vi, nullptr, &m_views[i]) != VK_SUCCESS)
            return false;
    }

    // One acquire semaphore per swapchain image: a presented image's semaphore
    // must not be reused until that image is re-acquired.
    m_acquireSemaphores.resize(imgCount);
    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < imgCount; i++) {
        if (vkCreateSemaphore(m_ctx.device, &semInfo, nullptr, &m_acquireSemaphores[i]) != VK_SUCCESS)
            return false;
    }
    return true;
}

void VkRenderer::destroySwapchain() {
    for (auto fb : m_framebuffers) vkDestroyFramebuffer(m_ctx.device, fb, nullptr);
    m_framebuffers.clear();
    for (auto v : m_views) vkDestroyImageView(m_ctx.device, v, nullptr);
    m_views.clear();
    for (auto s : m_acquireSemaphores) vkDestroySemaphore(m_ctx.device, s, nullptr);
    m_acquireSemaphores.clear();
    if (m_swapchain) vkDestroySwapchainKHR(m_ctx.device, m_swapchain, nullptr);
    m_swapchain = VK_NULL_HANDLE;
    if (m_depthView) vkDestroyImageView(m_ctx.device, m_depthView, nullptr);
    if (m_depthImage) vkDestroyImage(m_ctx.device, m_depthImage, nullptr);
    if (m_depthMem) vkFreeMemory(m_ctx.device, m_depthMem, nullptr);
    m_depthView = VK_NULL_HANDLE;
    m_depthImage = VK_NULL_HANDLE;
    m_depthMem = VK_NULL_HANDLE;
    m_swapchainValid = false;
}

bool VkRenderer::createDepthResources() {
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(m_ctx.phys, depthFormat, &props);
    if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        depthFormat = VK_FORMAT_D24_UNORM_S8_UINT;

    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = depthFormat;
    ii.extent = {m_extent.width, m_extent.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_ctx.device, &ii, nullptr, &m_depthImage) != VK_SUCCESS) return false;

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(m_ctx.device, m_depthImage, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = vkutil::findMemoryType(m_ctx.phys, mr.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_ctx.device, &ai, nullptr, &m_depthMem) != VK_SUCCESS) return false;
    vkBindImageMemory(m_ctx.device, m_depthImage, m_depthMem, 0);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = m_depthImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = depthFormat;
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(m_ctx.device, &vi, nullptr, &m_depthView) != VK_SUCCESS) return false;

    m_framebuffers.resize(m_views.size());
    for (size_t i = 0; i < m_views.size(); i++) {
        VkImageView attachments[2] = {m_views[i], m_depthView};
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = m_renderPass;
        fi.attachmentCount = 2;
        fi.pAttachments = attachments;
        fi.width = m_extent.width;
        fi.height = m_extent.height;
        fi.layers = 1;
        if (vkCreateFramebuffer(m_ctx.device, &fi, nullptr, &m_framebuffers[i]) != VK_SUCCESS)
            return false;
    }
    return true;
}

bool VkRenderer::createRenderPass() {
    // Depth format must be decided up-front; the render pass is created with it.
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(m_ctx.phys, depthFormat, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        depthFormat = VK_FORMAT_D24_UNORM_S8_UINT;

    VkAttachmentDescription color{};
    color.format = m_format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format = depthFormat;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subs[3] = {};
    for (int i = 0; i < 3; i++) {
        subs[i].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subs[i].colorAttachmentCount = 1;
        subs[i].pColorAttachments = &colorRef;
        subs[i].pDepthStencilAttachment = &depthRef;
    }

    VkSubpassDependency deps[4] = {};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = 1;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    deps[2].srcSubpass = 1;
    deps[2].dstSubpass = 2;
    deps[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[2].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    deps[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    deps[3].srcSubpass = 2;
    deps[3].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[3].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[3].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[3].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    VkAttachmentDescription attachments[2] = {color, depth};
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 2;
    rp.pAttachments = attachments;
    rp.subpassCount = 3;
    rp.pSubpasses = subs;
    rp.dependencyCount = 4;
    rp.pDependencies = deps;
    if (vkCreateRenderPass(m_ctx.device, &rp, nullptr, &m_renderPass) != VK_SUCCESS)
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// Pipelines
// ---------------------------------------------------------------------------
bool VkRenderer::createPipelines() {
    VkShaderModule worldVS = loadShaderModule(m_ctx.device, shaderPath("world.vert"));
    VkShaderModule worldFS = loadShaderModule(m_ctx.device, shaderPath("world.frag"));
    VkShaderModule boxVS = loadShaderModule(m_ctx.device, shaderPath("box.vert"));
    VkShaderModule boxFS = loadShaderModule(m_ctx.device, shaderPath("box.frag"));
    VkShaderModule crossVS = loadShaderModule(m_ctx.device, shaderPath("cross.vert"));
    VkShaderModule crossFS = loadShaderModule(m_ctx.device, shaderPath("cross.frag"));
    VkShaderModule crackVS = loadShaderModule(m_ctx.device, shaderPath("crack.vert"));
    VkShaderModule crackFS = loadShaderModule(m_ctx.device, shaderPath("crack.frag"));
    if (!worldVS || !worldFS || !boxVS || !boxFS || !crossVS || !crossFS || !crackVS || !crackFS) return false;

    VkPipelineShaderStageCreateInfo stages[2] = {
        stageInfo(VK_SHADER_STAGE_VERTEX_BIT, worldVS),
        stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT, worldFS),
    };

    // World: pos(3) normal(3) uv(2) = stride 32
    VkVertexInputBindingDescription worldBind{0, 32, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription worldAttr[3] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, 24},
    };
    VkPipelineVertexInputStateCreateInfo worldVi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    worldVi.vertexBindingDescriptionCount = 1;
    worldVi.pVertexBindingDescriptions = &worldBind;
    worldVi.vertexAttributeDescriptionCount = 3;
    worldVi.pVertexAttributeDescriptions = worldAttr;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vs.viewportCount = 1;
    vs.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_GREATER; // reversed-Z depth

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dci{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dci.dynamicStateCount = 2;
    dci.pDynamicStates = dyn;

    auto makeWorldPipeline = [&](VkPipeline& out, VkPipelineLayout layout, int subpass,
                                 bool depthWrite, bool blend) {
        VkPipelineDepthStencilStateCreateInfo dsW = ds;
        dsW.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        VkPipelineColorBlendAttachmentState cbaW = cba;
        VkPipelineColorBlendStateCreateInfo cbW = cb;
        if (blend) {
            cbaW.blendEnable = VK_TRUE;
            cbaW.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            cbaW.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            cbaW.colorBlendOp = VK_BLEND_OP_ADD;
            cbaW.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            cbaW.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            cbaW.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        cbW.pAttachments = &cbaW;

        VkGraphicsPipelineCreateInfo gi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gi.stageCount = 2;
        gi.pStages = stages;
        gi.pVertexInputState = &worldVi;
        gi.pInputAssemblyState = &ia;
        gi.pViewportState = &vs;
        gi.pRasterizationState = &rs;
        gi.pMultisampleState = &ms;
        gi.pDepthStencilState = &dsW;
        gi.pColorBlendState = &cbW;
        gi.pDynamicState = &dci;
        gi.layout = layout;
        gi.renderPass = m_renderPass;
        gi.subpass = (uint32_t)subpass;
        return vkCreateGraphicsPipelines(m_ctx.device, VK_NULL_HANDLE, 1, &gi, nullptr, &out) == VK_SUCCESS;
    };

    if (!makeWorldPipeline(m_worldSolid, m_worldLayout, 0, true, false)) return false;
    if (!makeWorldPipeline(m_worldWater, m_worldLayout, 1, false, true)) return false;

    // Box / crosshair: pos(3) = stride 12, LINE_LIST
    VkVertexInputBindingDescription uiBind{0, 12, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription uiAttr{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
    VkPipelineVertexInputStateCreateInfo uiVi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    uiVi.vertexBindingDescriptionCount = 1;
    uiVi.pVertexBindingDescriptions = &uiBind;
    uiVi.vertexAttributeDescriptionCount = 1;
    uiVi.pVertexAttributeDescriptions = &uiAttr;

    VkPipelineInputAssemblyStateCreateInfo iaLine{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    iaLine.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    auto makeUiPipeline = [&](VkPipeline& out, VkPipelineLayout layout,
                              VkShaderModule vertMod, VkShaderModule fragMod, bool depthTest) {
        VkPipelineShaderStageCreateInfo st[2] = {
            stageInfo(VK_SHADER_STAGE_VERTEX_BIT, vertMod),
            stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragMod),
        };
        VkPipelineDepthStencilStateCreateInfo dsU = ds;
        dsU.depthWriteEnable = VK_FALSE;
        dsU.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
        VkGraphicsPipelineCreateInfo gi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gi.stageCount = 2;
        gi.pStages = st;
        gi.pVertexInputState = &uiVi;
        gi.pInputAssemblyState = &iaLine;
        gi.pViewportState = &vs;
        gi.pRasterizationState = &rs;
        gi.pMultisampleState = &ms;
        gi.pDepthStencilState = &dsU;
        gi.pColorBlendState = &cb;
        gi.pDynamicState = &dci;
        gi.layout = layout;
        gi.renderPass = m_renderPass;
        gi.subpass = 2;
        return vkCreateGraphicsPipelines(m_ctx.device, VK_NULL_HANDLE, 1, &gi, nullptr, &out) == VK_SUCCESS;
    };

    if (!makeUiPipeline(m_box, m_boxLayout, boxVS, boxFS, true)) return false;
    if (!makeUiPipeline(m_cross, m_crossLayout, crossVS, crossFS, false)) return false;

    // Crack overlay: world vertex format, UI subpass, soft alpha blend so the
    // black crack lines darken the block face beneath them.
    {
        VkPipelineShaderStageCreateInfo crackStages[2] = {
            stageInfo(VK_SHADER_STAGE_VERTEX_BIT, crackVS),
            stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT, crackFS),
        };
        VkPipelineDepthStencilStateCreateInfo dsC = ds;
        dsC.depthWriteEnable = VK_FALSE;
        VkPipelineColorBlendAttachmentState cbaC = cba;
        cbaC.blendEnable = VK_TRUE;
        cbaC.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cbaC.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cbaC.colorBlendOp = VK_BLEND_OP_ADD;
        cbaC.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cbaC.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cbaC.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cbC = cb;
        cbC.pAttachments = &cbaC;
        VkGraphicsPipelineCreateInfo gi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gi.stageCount = 2;
        gi.pStages = crackStages;
        gi.pVertexInputState = &worldVi;
        gi.pInputAssemblyState = &ia;
        gi.pViewportState = &vs;
        gi.pRasterizationState = &rs;
        gi.pMultisampleState = &ms;
        gi.pDepthStencilState = &dsC;
        gi.pColorBlendState = &cbC;
        gi.pDynamicState = &dci;
        gi.layout = m_crackLayout;
        gi.renderPass = m_renderPass;
        gi.subpass = 2;
        if (vkCreateGraphicsPipelines(m_ctx.device, VK_NULL_HANDLE, 1, &gi, nullptr, &m_crack) != VK_SUCCESS)
            return false;
    }

    vkDestroyShaderModule(m_ctx.device, crackFS, nullptr);
    vkDestroyShaderModule(m_ctx.device, crackVS, nullptr);
    vkDestroyShaderModule(m_ctx.device, crossFS, nullptr);
    vkDestroyShaderModule(m_ctx.device, crossVS, nullptr);
    vkDestroyShaderModule(m_ctx.device, boxFS, nullptr);
    vkDestroyShaderModule(m_ctx.device, boxVS, nullptr);
    vkDestroyShaderModule(m_ctx.device, worldFS, nullptr);
    vkDestroyShaderModule(m_ctx.device, worldVS, nullptr);
    return true;
}

// ---------------------------------------------------------------------------
// Texture atlas
// ---------------------------------------------------------------------------
namespace {
void transitionImage(VkCommandBuffer cmd, VkImage img, VkImageLayout oldL, VkImageLayout newL,
                     uint32_t mipLevels) {
    VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    bar.image = img;
    bar.oldLayout = oldL;
    bar.newLayout = newL;
    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};

    VkPipelineStageFlags srcStage, dstStage;
    if (oldL == VK_IMAGE_LAYOUT_UNDEFINED && newL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        bar.srcAccessMask = 0;
        bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    } else {
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &bar);
}
}

bool VkRenderer::createAtlas(const TextureAtlas& atlas) {
    const auto& levels = atlas.levels();
    VkDeviceSize total = 0;
    for (auto& l : levels) total += (VkDeviceSize)l.px.size();

    // Staging buffer with all mip levels laid out sequentially.
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    std::vector<unsigned char> stagingData((size_t)total);
    {
        size_t off = 0;
        for (auto& l : levels) {
            std::memcpy(stagingData.data() + off, l.px.data(), l.px.size());
            off += l.px.size();
        }
    }
    if (!vkutil::createHostBuffer(m_ctx, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  staging, stagingMem, stagingData.data())) {
        return false;
    }

    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {TextureAtlas::SIZE, TextureAtlas::SIZE, 1};
    ii.mipLevels = (uint32_t)levels.size();
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_ctx.device, &ii, nullptr, &m_atlasImage) != VK_SUCCESS) return false;

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(m_ctx.device, m_atlasImage, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = vkutil::findMemoryType(m_ctx.phys, mr.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_ctx.device, &ai, nullptr, &m_atlasMem) != VK_SUCCESS) return false;
    vkBindImageMemory(m_ctx.device, m_atlasImage, m_atlasMem, 0);

    VkCommandBuffer cmd = vkutil::beginOneShot(m_ctx);
    transitionImage(cmd, m_atlasImage, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)levels.size());
    VkDeviceSize offset = 0;
    for (uint32_t m = 0; m < levels.size(); m++) {
        uint32_t w = (uint32_t)levels[m].w;
        VkBufferImageCopy reg{};
        reg.bufferOffset = offset;
        reg.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1};
        reg.imageExtent = {w, w, 1};
        vkCmdCopyBufferToImage(cmd, staging, m_atlasImage,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &reg);
        offset += levels[m].px.size();
    }
    transitionImage(cmd, m_atlasImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, (uint32_t)levels.size());
    vkutil::endOneShot(m_ctx, cmd);

    vkDestroyBuffer(m_ctx.device, staging, nullptr);
    vkFreeMemory(m_ctx.device, stagingMem, nullptr);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = m_atlasImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, (uint32_t)levels.size(), 0, 1};
    if (vkCreateImageView(m_ctx.device, &vi, nullptr, &m_atlasView) != VK_SUCCESS) return false;

    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.mipLodBias = 0.0f;
    si.anisotropyEnable = m_anisotropy > 1 ? VK_TRUE : VK_FALSE;
    si.maxAnisotropy = (float)m_anisotropy;
    si.minLod = 0.0f;
    si.maxLod = (float)(levels.size() - 1);
    if (vkCreateSampler(m_ctx.device, &si, nullptr, &m_atlasSampler) != VK_SUCCESS) return false;

    // Crack overlay sampler: NEAREST filtering so the crack texels stay crisp
    // and match the pixel-art look of the block textures. Locked to mip 0:
    // lower mips average the alpha channel, which would fade the thin crack
    // lines out and make them flicker as the camera moves.
    VkSamplerCreateInfo cs{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    cs.magFilter = VK_FILTER_NEAREST;
    cs.minFilter = VK_FILTER_NEAREST;
    cs.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    cs.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    cs.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    cs.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    cs.mipLodBias = 0.0f;
    cs.anisotropyEnable = VK_FALSE;
    cs.minLod = 0.0f;
    cs.maxLod = 0.0f; // only level 0
    if (vkCreateSampler(m_ctx.device, &cs, nullptr, &m_crackSampler) != VK_SUCCESS) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Descriptors, buffers, per-frame resources
// ---------------------------------------------------------------------------
bool VkRenderer::createDescriptors() {
    VkDescriptorSetLayoutBinding worldBinds[2] = {};
    worldBinds[0].binding = 0;
    worldBinds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    worldBinds[0].descriptorCount = 1;
    worldBinds[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    worldBinds[1].binding = 1;
    worldBinds[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    worldBinds[1].descriptorCount = 1;
    worldBinds[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo wl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    wl.bindingCount = 2;
    wl.pBindings = worldBinds;
    if (vkCreateDescriptorSetLayout(m_ctx.device, &wl, nullptr, &m_worldSetLayout) != VK_SUCCESS) return false;

    VkDescriptorSetLayoutBinding boxBind{};
    boxBind.binding = 0;
    boxBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    boxBind.descriptorCount = 1;
    boxBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo bl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    bl.bindingCount = 1;
    bl.pBindings = &boxBind;
    if (vkCreateDescriptorSetLayout(m_ctx.device, &bl, nullptr, &m_boxSetLayout) != VK_SUCCESS) return false;

    VkDescriptorSetLayoutCreateInfo cl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    if (vkCreateDescriptorSetLayout(m_ctx.device, &cl, nullptr, &m_crossSetLayout) != VK_SUCCESS) return false;

    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = (uint32_t)m_frames.size() * 3;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = (uint32_t)m_frames.size() * 2;
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = (uint32_t)m_frames.size() * 3;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(m_ctx.device, &pci, nullptr, &m_pool) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo wpl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    wpl.setLayoutCount = 1;
    wpl.pSetLayouts = &m_worldSetLayout;
    if (vkCreatePipelineLayout(m_ctx.device, &wpl, nullptr, &m_worldLayout) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo bpl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    bpl.setLayoutCount = 1;
    bpl.pSetLayouts = &m_boxSetLayout;
    VkPushConstantRange boxPC{};
    boxPC.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    boxPC.offset = 0;
    boxPC.size = 80; // mat4 + vec4
    bpl.pushConstantRangeCount = 1;
    bpl.pPushConstantRanges = &boxPC;
    if (vkCreatePipelineLayout(m_ctx.device, &bpl, nullptr, &m_boxLayout) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo cpl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    cpl.setLayoutCount = 1;
    cpl.pSetLayouts = &m_crossSetLayout;
    VkPushConstantRange crossPC{};
    crossPC.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    crossPC.offset = 0;
    crossPC.size = 16; // vec4
    cpl.pushConstantRangeCount = 1;
    cpl.pPushConstantRanges = &crossPC;
    if (vkCreatePipelineLayout(m_ctx.device, &cpl, nullptr, &m_crossLayout) != VK_SUCCESS) return false;

    // Crack overlay layout: world descriptor set (UBO + atlas) + mat4/vec4.
    VkPipelineLayoutCreateInfo crl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    crl.setLayoutCount = 1;
    crl.pSetLayouts = &m_worldSetLayout;
    VkPushConstantRange crackPC{};
    crackPC.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    crackPC.offset = 0;
    crackPC.size = 80; // mat4 + vec4
    crl.pushConstantRangeCount = 1;
    crl.pPushConstantRanges = &crackPC;
    if (vkCreatePipelineLayout(m_ctx.device, &crl, nullptr, &m_crackLayout) != VK_SUCCESS) return false;

    for (auto& f : m_frames) {
        if (!vkutil::createHostBuffer(m_ctx, sizeof(UboData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                      f.ubo, f.uboMem)) return false;
        vkMapMemory(m_ctx.device, f.uboMem, 0, sizeof(UboData), 0, &f.uboMap);

        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = m_pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &m_worldSetLayout;
        if (vkAllocateDescriptorSets(m_ctx.device, &ai, &f.worldSet) != VK_SUCCESS) return false;

        ai.pSetLayouts = &m_boxSetLayout;
        if (vkAllocateDescriptorSets(m_ctx.device, &ai, &f.boxSet) != VK_SUCCESS) return false;

        ai.pSetLayouts = &m_worldSetLayout;
        if (vkAllocateDescriptorSets(m_ctx.device, &ai, &f.crackSet) != VK_SUCCESS) return false;

        VkDescriptorBufferInfo binfo{f.ubo, 0, sizeof(UboData)};
        VkDescriptorImageInfo iinfo{m_atlasSampler, m_atlasView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet writes[2] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = f.worldSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &binfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = f.worldSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &iinfo;
        vkUpdateDescriptorSets(m_ctx.device, 2, writes, 0, nullptr);

        VkDescriptorBufferInfo b2{f.ubo, 0, sizeof(UboData)};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = f.boxSet;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &b2;
        vkUpdateDescriptorSets(m_ctx.device, 1, &w, 0, nullptr);

        VkDescriptorBufferInfo cb0{f.ubo, 0, sizeof(UboData)};
        VkDescriptorImageInfo ci0{m_crackSampler, m_atlasView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet cw[2] = {};
        cw[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cw[0].dstSet = f.crackSet;
        cw[0].dstBinding = 0;
        cw[0].descriptorCount = 1;
        cw[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        cw[0].pBufferInfo = &cb0;
        cw[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cw[1].dstSet = f.crackSet;
        cw[1].dstBinding = 1;
        cw[1].descriptorCount = 1;
        cw[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        cw[1].pImageInfo = &ci0;
        vkUpdateDescriptorSets(m_ctx.device, 2, cw, 0, nullptr);
    }
    return true;
}

bool VkRenderer::createUiBuffers() {
    static const float BOX[24][3] = {
        {0,0,0},{1,0,0},{1,0,0},{1,1,0},{1,1,0},{0,1,0},{0,1,0},{0,0,0},
        {0,0,1},{1,0,1},{1,0,1},{1,1,1},{1,1,1},{0,1,1},{0,1,1},{0,0,1},
        {0,0,0},{0,0,1},{1,0,0},{1,0,1},{1,1,0},{1,1,1},{0,1,0},{0,1,1},
    };
    static const float CROSS[4][3] = {
        {-0.02f, 0, 0}, {0.02f, 0, 0},
        {0, -0.02f, 0}, {0, 0.02f, 0},
    };
    if (!vkutil::createHostBuffer(m_ctx, sizeof(BOX), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  m_boxBuf, m_boxMem, BOX)) return false;
    if (!vkutil::createHostBuffer(m_ctx, sizeof(CROSS), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  m_crossBuf, m_crossMem, CROSS)) return false;

    // Unit cube (6 faces as triangle lists) with 0..1 UVs for the crack overlay.
    static const float CUBE_V[6][4][3] = {
        {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}},
        {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
        {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},
        {{1, 0, 0}, {1, 0, 1}, {0, 0, 1}, {0, 0, 0}},
        {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}},
        {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},
    };
    static const float CUBE_N[6][3] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const float CUV[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    static const int CT[6] = {0, 1, 2, 0, 2, 3};
    std::vector<float> crack(6 * 6 * 8);
    int k = 0;
    for (int f = 0; f < 6; f++)
        for (int c = 0; c < 6; c++) {
            const float* v = CUBE_V[f][CT[c]];
            crack[k++] = v[0]; crack[k++] = v[1]; crack[k++] = v[2];
            crack[k++] = CUBE_N[f][0]; crack[k++] = CUBE_N[f][1]; crack[k++] = CUBE_N[f][2];
            crack[k++] = CUV[CT[c]][0]; crack[k++] = CUV[CT[c]][1];
        }
    if (!vkutil::createHostBuffer(m_ctx, crack.size() * sizeof(float),
                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  m_crackBuf, m_crackMem, crack.data())) return false;
    return true;
}

void VkRenderer::createFrameResources() {
    // Align the frame-in-flight count with the swapchain image count so each
    // acquire semaphore is only reused when its image cycles back around.
    uint32_t count = (uint32_t)std::max<size_t>(2, m_images.size());
    m_frames.resize(count);
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = m_ctx.pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    for (auto& f : m_frames) {
        vkAllocateCommandBuffers(m_ctx.device, &ai, &f.cmd);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(m_ctx.device, &si, nullptr, &f.renderFinished);
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(m_ctx.device, &fi, nullptr, &f.inFlight);
    }
}

namespace vkutil {
void retireImpl(const RenderContext& ctx, VkBuffer buffer, VkDeviceMemory memory) {
    if (ctx.owner) ctx.owner->retireBuffer(buffer, memory);
}
} // namespace vkutil

// ---------------------------------------------------------------------------
// Init / shutdown
// ---------------------------------------------------------------------------
void VkRenderer::retireBuffer(VkBuffer b, VkDeviceMemory m) {
    if (!b || m_frames.empty()) return;
    // Buffers retired now were referenced by the frame submitted last CPU
    // cycle. Route them to the frame whose fence is waited last in the cycle
    // (frame index + N - 1), which is guaranteed done by then.
    size_t target = (m_frameIndex + m_frames.size() - 1) % m_frames.size();
    m_frames[target].pendingDestroy.push_back({b, m});
}

bool VkRenderer::init(GLFWwindow* window, const TextureAtlas& atlas) {
    m_window = window;
    m_ctx.owner = this;
    if (!createInstance("Minkraft")) return false;
    if (!createSurface(window)) { fprintf(stderr, "[vulkan] failed to create surface\n"); return false; }
    if (!createDevice()) return false;
    if (!createSwapchain(window)) return false;
    if (!createRenderPass()) return false;
    if (!createDepthResources()) return false;
    if (!createAtlas(atlas)) return false;
    createFrameResources();
    if (!createDescriptors()) return false;
    if (!createPipelines()) return false;
    if (!createUiBuffers()) return false;
    m_swapchainValid = true;
    return true;
}

void VkRenderer::destroyPipelines() {
    if (m_worldSolid) vkDestroyPipeline(m_ctx.device, m_worldSolid, nullptr);
    if (m_worldWater) vkDestroyPipeline(m_ctx.device, m_worldWater, nullptr);
    if (m_box) vkDestroyPipeline(m_ctx.device, m_box, nullptr);
    if (m_cross) vkDestroyPipeline(m_ctx.device, m_cross, nullptr);
    if (m_crack) vkDestroyPipeline(m_ctx.device, m_crack, nullptr);
    if (m_worldLayout) vkDestroyPipelineLayout(m_ctx.device, m_worldLayout, nullptr);
    if (m_boxLayout) vkDestroyPipelineLayout(m_ctx.device, m_boxLayout, nullptr);
    if (m_crossLayout) vkDestroyPipelineLayout(m_ctx.device, m_crossLayout, nullptr);
    if (m_crackLayout) vkDestroyPipelineLayout(m_ctx.device, m_crackLayout, nullptr);
    m_worldSolid = m_worldWater = m_box = m_cross = m_crack = VK_NULL_HANDLE;
    m_worldLayout = m_boxLayout = m_crossLayout = m_crackLayout = VK_NULL_HANDLE;
}

void VkRenderer::shutdown() {
    if (!m_ctx.device) return;
    vkDeviceWaitIdle(m_ctx.device);

    destroySwapchain();
    destroyPipelines();

    if (m_atlasView) vkDestroyImageView(m_ctx.device, m_atlasView, nullptr);
    if (m_atlasImage) vkDestroyImage(m_ctx.device, m_atlasImage, nullptr);
    if (m_atlasMem) vkFreeMemory(m_ctx.device, m_atlasMem, nullptr);
    if (m_atlasSampler) vkDestroySampler(m_ctx.device, m_atlasSampler, nullptr);
    if (m_crackSampler) vkDestroySampler(m_ctx.device, m_crackSampler, nullptr);

    if (m_worldSetLayout) vkDestroyDescriptorSetLayout(m_ctx.device, m_worldSetLayout, nullptr);
    if (m_boxSetLayout) vkDestroyDescriptorSetLayout(m_ctx.device, m_boxSetLayout, nullptr);
    if (m_crossSetLayout) vkDestroyDescriptorSetLayout(m_ctx.device, m_crossSetLayout, nullptr);
    if (m_pool) vkDestroyDescriptorPool(m_ctx.device, m_pool, nullptr);

    if (m_boxBuf) vkDestroyBuffer(m_ctx.device, m_boxBuf, nullptr);
    if (m_boxMem) vkFreeMemory(m_ctx.device, m_boxMem, nullptr);
    if (m_crossBuf) vkDestroyBuffer(m_ctx.device, m_crossBuf, nullptr);
    if (m_crossMem) vkFreeMemory(m_ctx.device, m_crossMem, nullptr);
    if (m_crackBuf) vkDestroyBuffer(m_ctx.device, m_crackBuf, nullptr);
    if (m_crackMem) vkFreeMemory(m_ctx.device, m_crackMem, nullptr);
    if (m_shotBuf) vkDestroyBuffer(m_ctx.device, m_shotBuf, nullptr);
    if (m_shotMem) vkFreeMemory(m_ctx.device, m_shotMem, nullptr);

    for (auto& f : m_frames) {
        if (f.cmd) vkFreeCommandBuffers(m_ctx.device, m_ctx.pool, 1, &f.cmd);
        if (f.renderFinished) vkDestroySemaphore(m_ctx.device, f.renderFinished, nullptr);
        if (f.inFlight) vkDestroyFence(m_ctx.device, f.inFlight, nullptr);
        if (f.ubo) vkDestroyBuffer(m_ctx.device, f.ubo, nullptr);
        if (f.uboMem) vkFreeMemory(m_ctx.device, f.uboMem, nullptr);
        for (auto& [b, mem] : f.pendingDestroy) {
            vkDestroyBuffer(m_ctx.device, b, nullptr);
            vkFreeMemory(m_ctx.device, mem, nullptr);
        }
        f.pendingDestroy.clear();
    }
    m_frames.clear();

    if (m_ctx.pool) vkDestroyCommandPool(m_ctx.device, m_ctx.pool, nullptr);
    if (m_renderPass) vkDestroyRenderPass(m_ctx.device, m_renderPass, nullptr);
    if (m_ctx.device) vkDestroyDevice(m_ctx.device, nullptr);
    if (m_surface) vkDestroySurfaceKHR(m_ctx.instance, m_surface, nullptr);
    if (m_debugMessenger) {
        auto destroyMsg = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_ctx.instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroyMsg) destroyMsg(m_ctx.instance, m_debugMessenger, nullptr);
    }
    if (m_ctx.instance) vkDestroyInstance(m_ctx.instance, nullptr);
    m_ctx = RenderContext{};
}

bool VkRenderer::saveScreenshot(const char* path) {
    if (!m_doScreenshot || !m_shotBuf) return false;

    // Wait for the copy (submitted in the last frame) to finish.
    vkDeviceWaitIdle(m_ctx.device);

    VkDeviceSize size = (VkDeviceSize)m_extent.width * m_extent.height * 4;
    void* data = nullptr;
    if (vkMapMemory(m_ctx.device, m_shotMem, 0, size, 0, &data) != VK_SUCCESS) return false;

    uint32_t w = m_extent.width, h = m_extent.height;
    FILE* f = fopen(path, "wb");
    if (!f) { vkUnmapMemory(m_ctx.device, m_shotMem); return false; }

    int rowSize = (int)((w * 3 + 3) & ~3);
    int fileSize = 54 + rowSize * (int)h;
    unsigned char header[54] = {0};
    header[0] = 'B'; header[1] = 'M';
    header[2] = (unsigned char)fileSize; header[3] = (unsigned char)(fileSize >> 8);
    header[4] = (unsigned char)(fileSize >> 16); header[5] = (unsigned char)(fileSize >> 24);
    header[10] = 54;
    header[14] = 40;
    header[18] = (unsigned char)w; header[19] = (unsigned char)(w >> 8);
    header[20] = (unsigned char)(w >> 16); header[21] = (unsigned char)(w >> 24);
    header[22] = (unsigned char)h; header[23] = (unsigned char)(h >> 8);
    header[24] = (unsigned char)(h >> 16); header[25] = (unsigned char)(h >> 24);
    header[26] = 1;
    header[28] = 24;
    fwrite(header, 1, 54, f);

    const unsigned char* src = (const unsigned char*)data;
    std::vector<unsigned char> rowBuf(rowSize);
    for (uint32_t y = 0; y < h; y++) {
        // Vulkan buffer rows are top-down, BGRA (swapchain is B8G8R8A8).
        // BMP rows are bottom-up and expect B,G,R.
        const unsigned char* in = src + (size_t)(h - 1 - y) * w * 4;
        int i = 0;
        for (uint32_t x = 0; x < w; x++) {
            rowBuf[i++] = in[x * 4 + 0]; // B
            rowBuf[i++] = in[x * 4 + 1]; // G
            rowBuf[i++] = in[x * 4 + 2]; // R
        }
        fwrite(rowBuf.data(), 1, rowSize, f);
    }
    fclose(f);
    vkUnmapMemory(m_ctx.device, m_shotMem);
    m_doScreenshot = false;
    return true;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void VkRenderer::requestScreenshot() {
    VkDeviceSize size = (VkDeviceSize)m_extent.width * m_extent.height * 4;
    if (m_shotBuf && m_shotSize != size) {
        vkutil::retire(m_ctx, m_shotBuf, m_shotMem);
        m_shotBuf = VK_NULL_HANDLE;
        m_shotMem = VK_NULL_HANDLE;
    }
    if (!m_shotBuf) {
        if (!vkutil::createHostBuffer(m_ctx, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                      m_shotBuf, m_shotMem)) {
            fprintf(stderr, "[vulkan] failed to allocate screenshot buffer\n");
            return;
        }
        m_shotSize = size;
    }
    m_doScreenshot = true;
}
void VkRenderer::updateUbo(Frame& f, const FrameState& fs) {
    UboData u;
    u.viewProj = fs.viewProj;
    u.camPos[0] = fs.camPos.x; u.camPos[1] = fs.camPos.y; u.camPos[2] = fs.camPos.z; u.camPos[3] = 1.0f;
    u.fogColor[0] = fs.fogColor.x; u.fogColor[1] = fs.fogColor.y; u.fogColor[2] = fs.fogColor.z; u.fogColor[3] = 1.0f;
    u.fogParams[0] = fs.fogNear; u.fogParams[1] = fs.fogFar; u.fogParams[2] = 0; u.fogParams[3] = 0;
    std::memcpy(f.uboMap, &u, sizeof(UboData));
}

void VkRenderer::recordFrame(Frame& f, World& world, const FrameState& fs) {
    VkCommandBuffer cmd = f.cmd;
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &bi);

    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = m_renderPass;
    rp.framebuffer = m_framebuffers[m_imageIndex];
    rp.renderArea = {{0, 0}, m_extent};
    VkClearValue clears[2] = {};
    clears[0].color = {{fs.fogColor.x, fs.fogColor.y, fs.fogColor.z, 1.0f}};
    clears[1].depthStencil = {0.0f, 0}; // reversed-Z: clear to far (0)
    rp.clearValueCount = 2;
    rp.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0, 0, (float)m_extent.width, (float)m_extent.height, 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, m_extent};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // ---- Subpass 0: opaque terrain ----
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_worldSolid);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_worldLayout,
                            0, 1, &f.worldSet, 0, nullptr);
    {
        Frustum fr;
        fr.extract(fs.viewProj);
        VkDeviceSize offset = 0;
        world.forEachChunk([&](const Chunk& c) {
            if (!fr.visible((float)(c.cx * Chunk::CX), 0.0f, (float)(c.cz * Chunk::CZ),
                            (float)(c.cx * Chunk::CX + Chunk::CX), (float)Chunk::CY,
                            (float)(c.cz * Chunk::CZ + Chunk::CZ)))
                return;
            if (c.solidCount() > 0) {
                VkBuffer b = c.solidBuffer();
                vkCmdBindVertexBuffers(cmd, 0, 1, &b, &offset);
                vkCmdDraw(cmd, c.solidCount(), 1, 0, 0);
            }
        });
    }
    vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);

    // ---- Subpass 1: water ----
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_worldWater);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_worldLayout,
                            0, 1, &f.worldSet, 0, nullptr);
    {
        Frustum fr;
        fr.extract(fs.viewProj);
        VkDeviceSize offset = 0;
        world.forEachChunk([&](const Chunk& c) {
            if (!fr.visible((float)(c.cx * Chunk::CX), 0.0f, (float)(c.cz * Chunk::CZ),
                            (float)(c.cx * Chunk::CX + Chunk::CX), (float)Chunk::CY,
                            (float)(c.cz * Chunk::CZ + Chunk::CZ)))
                return;
            if (c.waterCount() > 0) {
                VkBuffer b = c.waterBuffer();
                vkCmdBindVertexBuffers(cmd, 0, 1, &b, &offset);
                vkCmdDraw(cmd, c.waterCount(), 1, 0, 0);
            }
        });
    }
    vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);

    // ---- Subpass 2: UI (crack overlay + selection box + crosshair) ----
    if (fs.showCrack && fs.crackStage >= 0 && fs.crackStage < 8) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_crack);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_crackLayout,
                                0, 1, &f.crackSet, 0, nullptr);
        // Slightly hull the cube so the cracks sit just in front of the faces.
        Mat4 grow = identity();
        grow.m[0] = grow.m[5] = grow.m[10] = 1.001f;
        Mat4 model = mat4Mul(translation(fs.crackPos), grow);
        float u0, v0, u1, v1;
        TextureAtlas::tileUV(TILE_CRACK0 + fs.crackStage, u0, v0, u1, v1);
        struct CrackPC {
            Mat4 model;
            float uvRect[4];
        } pc;
        pc.model = model;
        pc.uvRect[0] = u0; pc.uvRect[1] = v0; pc.uvRect[2] = u1; pc.uvRect[3] = v1;
        vkCmdPushConstants(cmd, m_crackLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_crackBuf, &offset);
        // Draw one quad per exposed face (each face is 6 vertices).
        uint32_t mask = (uint32_t)fs.crackFaces;
        for (int f = 0; f < 6; f++) {
            if (mask & (1u << f))
                vkCmdDraw(cmd, 6, 1, f * 6, 0);
        }
    }
    if (fs.showBox) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_box);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_boxLayout,
                                0, 1, &f.boxSet, 0, nullptr);
        struct BoxPC {
            Mat4 model;
            float color[4];
        } pc;
        pc.model = translation(fs.boxPos);
        pc.color[0] = 0.05f; pc.color[1] = 0.05f; pc.color[2] = 0.05f; pc.color[3] = 1.0f;
        vkCmdPushConstants(cmd, m_boxLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_boxBuf, &offset);
        vkCmdDraw(cmd, 24, 1, 0, 0);
    }
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_cross);
        struct CrossPC { float color[4]; } pc;
        pc.color[0] = 1; pc.color[1] = 1; pc.color[2] = 1; pc.color[3] = 1;
        vkCmdPushConstants(cmd, m_crossLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_crossBuf, &offset);
        vkCmdDraw(cmd, 4, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);

    // Screenshot: copy the swapchain image to a host-readable buffer before present.
    if (m_doScreenshot && m_shotBuf) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.image = m_images[m_imageIndex];
        b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {m_extent.width, m_extent.height, 1};
        vkCmdCopyImageToBuffer(cmd, m_images[m_imageIndex],
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_shotBuf, 1, &region);
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = 0;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

    vkEndCommandBuffer(cmd);
}

void VkRenderer::recreateSwapchain(GLFWwindow* w) {
    int width, height;
    glfwGetFramebufferSize(w, &width, &height);
    if (width == 0 || height == 0) {
        m_swapchainValid = false;
        return;
    }
    vkDeviceWaitIdle(m_ctx.device);
    destroySwapchain();
    if (createSwapchain(w)) {
        createDepthResources();
        m_swapchainValid = true;
    } else {
        fprintf(stderr, "[vulkan] swapchain recreation failed\n");
    }
}

void VkRenderer::drawFrame(World& world, const FrameState& fs) {
    if (!m_swapchainValid) return;

    Frame& f = m_frames[m_frameIndex];
    vkWaitForFences(m_ctx.device, 1, &f.inFlight, VK_TRUE, UINT64_MAX);

    // Fence is done: buffers retired a full cycle ago are no longer referenced.
    for (auto& [b, mem] : f.pendingDestroy) {
        vkDestroyBuffer(m_ctx.device, b, nullptr);
        vkFreeMemory(m_ctx.device, mem, nullptr);
    }
    f.pendingDestroy.clear();

    VkSemaphore acquireSem = m_acquireSemaphores[m_imageIndex % m_acquireSemaphores.size()];
    VkResult r = vkAcquireNextImageKHR(m_ctx.device, m_swapchain, UINT64_MAX,
                                       acquireSem, VK_NULL_HANDLE, &m_imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain(m_window);
        return;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return;

    updateUbo(f, fs);
    vkResetFences(m_ctx.device, 1, &f.inFlight);
    vkResetCommandBuffer(f.cmd, 0);
    recordFrame(f, world, fs);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &acquireSem;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &f.renderFinished;
    vkQueueSubmit(m_ctx.queue, 1, &si, f.inFlight);

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &f.renderFinished;
    pi.swapchainCount = 1;
    pi.pSwapchains = &m_swapchain;
    pi.pImageIndices = &m_imageIndex;
    VkResult pr = vkQueuePresentKHR(m_ctx.queue, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain(m_window);
    }

    m_frameIndex = (m_frameIndex + 1) % (uint32_t)m_frames.size();
}
