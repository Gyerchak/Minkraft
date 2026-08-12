#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cstdlib>

// Loads a precompiled SPIR-V file (built by glslc at build time) and creates a
// shader module. No runtime GLSL compilation, so startup is fast.
inline VkShaderModule loadShaderModule(VkDevice device, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[shader] cannot open %s\n", path);
        return VK_NULL_HANDLE;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint32_t> code((size + 3) / 4);
    size_t got = fread(code.data(), 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = (size_t)size;
    ci.pCode = code.data();
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS) {
        fprintf(stderr, "[shader] failed to create module from %s\n", path);
        return VK_NULL_HANDLE;
    }
    return mod;
}

// Builds a path to shaders/<name>.spv relative to the working dir or exe dir.
inline const char* shaderPath(const char* name) {
    static std::string cached;
    if (cached.empty()) {
        const char* dir = getenv("MINKRAFT_SHADERS");
        if (dir && *dir) cached = std::string(dir);
        else cached = "shaders";
        // Check relative to the executable too.
        FILE* probe = fopen((cached + "/world.vert.spv").c_str(), "rb");
        if (!probe) {
            char buf[4096];
            ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                std::string exe = buf;
                size_t slash = exe.find_last_of('/');
                if (slash != std::string::npos) exe = exe.substr(0, slash);
                std::string alt = exe + "/shaders";
                probe = fopen((alt + "/world.vert.spv").c_str(), "rb");
                if (probe) cached = alt;
            }
        }
        if (probe) fclose(probe);
    }
    static thread_local std::string full;
    full = cached + "/" + name + ".spv";
    return full.c_str();
}
