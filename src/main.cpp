#include <GLFW/glfw3.h>

#include <cstdio>
#include <cmath>
#include <ctime>
#include <string>

#include "Math.h"
#include "Noise.h"
#include "Block.h"
#include "Camera.h"
#include "Texture.h"
#include "World.h"
#include "Player.h"
#include "VkRenderer.h"
#include "Settings.h"

#include <sys/stat.h>
#include <unistd.h>
#include <thread>
#include <chrono>

// Locates the textures/ pack: first relative to the working directory, then
// next to the executable (so the build dir is self-contained).
static const char* findTexturesDir() {
    static std::string found;
    if (!found.empty()) return found.c_str();
    struct stat sb;
    if (stat("textures", &sb) == 0 && S_ISDIR(sb.st_mode)) {
        found = "textures";
        return found.c_str();
    }
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        std::string exe = buf;
        size_t slash = exe.find_last_of('/');
        if (slash != std::string::npos) exe = exe.substr(0, slash);
        std::string dir = exe + "/textures";
        if (stat(dir.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode)) {
            found = dir;
            return found.c_str();
        }
    }
    return ""; // no pack -> procedural fallback for every tile
}

// Returns the directory containing the executable (for finding assets).
static std::string exeDir() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        std::string exe = buf;
        size_t slash = exe.find_last_of('/');
        if (slash != std::string::npos) return exe.substr(0, slash);
    }
    return "";
}

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static Camera g_cam;
static Player g_player;
static double g_lastX = 1280 / 2.0;
static double g_lastY = 720 / 2.0;
static bool g_firstMouse = true;
static bool g_keys[GLFW_KEY_LAST + 1] = {false};
static unsigned char g_placeBlock = GRASS;
static Settings g_settings;
static bool g_takeShot = false;

static void keyCallback(GLFWwindow* win, int key, int scancode, int action, int mods) {
    (void)win; (void)scancode; (void)mods;
    if (key < 0 || key > GLFW_KEY_LAST) return;
    if (action == GLFW_PRESS) g_keys[key] = true;
    else if (action == GLFW_RELEASE) g_keys[key] = false;

    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_1: g_placeBlock = GRASS; break;
            case GLFW_KEY_2: g_placeBlock = DIRT; break;
            case GLFW_KEY_3: g_placeBlock = STONE; break;
            case GLFW_KEY_4: g_placeBlock = SAND; break;
            case GLFW_KEY_5: g_placeBlock = PLANKS; break;
            case GLFW_KEY_6: g_placeBlock = LOG; break;
            case GLFW_KEY_F2: g_takeShot = true; break;
            default: break;
        }
    }
}

static void mouseCallback(GLFWwindow* win, double x, double y) {
    (void)win;
    if (g_firstMouse) {
        g_lastX = x;
        g_lastY = y;
        g_firstMouse = false;
    }
    double dx = x - g_lastX;
    double dy = y - g_lastY;
    g_lastX = x;
    g_lastY = y;
    g_cam.rotate((float)(dx * 0.12), (float)(-dy * 0.12));
}

// ---------------------------------------------------------------------------
// Raycast (DDA) for block picking.
// ---------------------------------------------------------------------------
static bool raycastBlock(World& world, const Vec3& origin, const Vec3& dir,
                         int& outX, int& outY, int& outZ, int& outFace) {
    int x = (int)std::floor(origin.x);
    int y = (int)std::floor(origin.y);
    int z = (int)std::floor(origin.z);

    int stepX = dir.x > 0 ? 1 : -1;
    int stepY = dir.y > 0 ? 1 : -1;
    int stepZ = dir.z > 0 ? 1 : -1;

    float tDeltaX = std::abs(1.0f / (dir.x != 0 ? dir.x : 1e-30f));
    float tDeltaY = std::abs(1.0f / (dir.y != 0 ? dir.y : 1e-30f));
    float tDeltaZ = std::abs(1.0f / (dir.z != 0 ? dir.z : 1e-30f));

    float tMaxX = (dir.x > 0 ? (x + 1 - origin.x) : (origin.x - x)) * tDeltaX;
    float tMaxY = (dir.y > 0 ? (y + 1 - origin.y) : (origin.y - y)) * tDeltaY;
    float tMaxZ = (dir.z > 0 ? (z + 1 - origin.z) : (origin.z - z)) * tDeltaZ;

    int face = -1;
    float t = 0;
    while (t <= g_settings.reach) {
        unsigned char b = world.getBlock(x, y, z);
        if (b != AIR && !blockDef(b).water) {
            outX = x; outY = y; outZ = z; outFace = face;
            return true;
        }
        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            x += stepX;
            t = tMaxX;
            tMaxX += tDeltaX;
            face = stepX > 0 ? FACE_NX : FACE_PX;
        } else if (tMaxY < tMaxZ) {
            y += stepY;
            t = tMaxY;
            tMaxY += tDeltaY;
            face = stepY > 0 ? FACE_NY : FACE_PY;
        } else {
            z += stepZ;
            t = tMaxZ;
            tMaxZ += tDeltaZ;
            face = stepZ > 0 ? FACE_NZ : FACE_PZ;
        }
    }
    return false;
}

int main() {
    // Load settings from settings.conf (working dir or next to the executable).
    g_settings.load(exeDir().c_str());

    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    int winW = g_settings.windowWidth;
    int winH = g_settings.windowHeight;
    GLFWmonitor* monitor = nullptr;
    if (g_settings.windowBorderless) {
        monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        winW = mode->width;
        winH = mode->height;
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    }

    GLFWwindow* window = glfwCreateWindow(winW, winH, "Minkraft", monitor, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwSwapInterval(g_settings.windowVsync ? 1 : 0);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Apply FOV and far plane from settings (far plane covers the render dist).
    g_cam.fov = g_settings.fovDegrees;
    g_cam.farPlane = (float)(g_settings.renderDistance + 2) * Chunk::CX;
    if (getenv("MINKRAFT_PERF"))
        fprintf(stderr, "[perf] settings rd=%d fov=%.1f reach=%.1f aniso=%d budget=%d vsync=%d\n",
                g_settings.renderDistance, g_settings.fovDegrees, g_settings.reach,
                g_settings.maxAnisotropy, g_settings.meshBudget, g_settings.windowVsync);

    // -------- World & assets --------
    double tStartup = glfwGetTime();
    uint32_t seed = (uint32_t)time(nullptr);
    const char* seedStr = getenv("MINKRAFT_SEED");
    if (seedStr) seed = (uint32_t)atoi(seedStr);
    World world(seed);

    TextureAtlas atlas;
    atlas.generate(findTexturesDir());

    double tRender = glfwGetTime();
    VkRenderer renderer;
    if (!renderer.init(window, atlas)) {
        fprintf(stderr, "Failed to initialize Vulkan renderer\n");
        return 1;
    }
    renderer.setAnisotropy(g_settings.maxAnisotropy);
    if (getenv("MINKRAFT_PERF")) fprintf(stderr, "[perf]  renderer=%.1fms\n", (glfwGetTime() - tRender) * 1000.0);
    world.setRenderContext(&renderer.ctx());
    world.setMeshBudget(g_settings.meshBudget);

    // -------- Spawn: find a grassy spot with dry land around it (avoid oceans) --------
    world.update(0, 0, g_settings.renderDistance);
    if (getenv("MINKRAFT_PERF")) fprintf(stderr, "[perf]  gen=%.1fms\n", (glfwGetTime() - tStartup) * 1000.0);
    double tScan = glfwGetTime();
    while (!world.meshQueueEmpty()) world.processMeshQueue(); // mesh the whole spawn area now
    if (getenv("MINKRAFT_PERF")) fprintf(stderr, "[perf]  mesh=%.1fms\n", (glfwGetTime() - tScan) * 1000.0);
    auto topSolid = [&](int x, int z) {
        for (int y = 62; y >= 0; y--)
            if (world.isSolid(x, y, z)) return y;
        return 0;
    };
    bool spawned = false;
    for (int r = 0; r < 64 && !spawned; r++) {
        for (int dz = -r; dz <= r && !spawned; dz++) {
            for (int dx = -r; dx <= r && !spawned; dx++) {
                int h = topSolid(dx, dz);
                if (h < 26 || h >= 46) continue;
                if (world.getBlock(dx, h, dz) != GRASS) continue;
                // Require a patch of dry land around this spot.
                int dry = 0;
                for (int dz2 = -3; dz2 <= 3; dz2++)
                    for (int dx2 = -3; dx2 <= 3; dx2++)
                        if (topSolid(dx + dx2, dz + dz2) >= 24) dry++;
                if (dry >= 30) {
                    g_player.pos = Vec3(dx + 0.5f, h + 1.01f, dz + 0.5f);
                    spawned = true;
                }
            }
        }
    }
    if (!spawned) g_player.pos = Vec3(0.5f, 60.0f, 0.5f);
    if (getenv("MINKRAFT_PERF"))
        fprintf(stderr, "[perf] startup=%.1fms spawn=%.0f,%.0f,%.0f\n",
                (glfwGetTime() - tStartup) * 1000.0, g_player.pos.x, g_player.pos.y, g_player.pos.z);

    double lastTime = glfwGetTime();
    double fpsAccum = 0;
    int fpsFrames = 0;

    while (!glfwWindowShouldClose(window)) {
        // Optional frame rate cap (when vsync is off).
        if (g_settings.maxFps > 0 && !g_settings.windowVsync) {
            double target = 1.0 / g_settings.maxFps;
            while (glfwGetTime() - lastTime < target) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        double now = glfwGetTime();
        float dt = (float)(now - lastTime);
        lastTime = now;
        if (dt > 0.1f) dt = 0.1f;

        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        // -------- Movement --------
        Vec3 f = g_cam.front();
        Vec3 r = g_cam.right();
        Vec3 wish(0, 0, 0);
        if (g_keys[GLFW_KEY_W]) wish = wish + f;
        if (g_keys[GLFW_KEY_S]) wish = wish - f;
        if (g_keys[GLFW_KEY_D]) wish = wish + r;
        if (g_keys[GLFW_KEY_A]) wish = wish - r;
        wish.y = 0; // horizontal movement only
        if (dot(wish, wish) > 0) {
            wish = normalize(wish);
            g_player.vel.x = wish.x * 4.5f;
            g_player.vel.z = wish.z * 4.5f;
        } else {
            g_player.vel.x *= 0.8f;
            g_player.vel.z *= 0.8f;
        }

        bool jump = g_keys[GLFW_KEY_SPACE];
        bool sneak = g_keys[GLFW_KEY_LEFT_SHIFT] || g_keys[GLFW_KEY_LEFT_CONTROL];
        g_player.update(world, dt, false, false, false, false, jump, sneak);
        g_cam.pos = g_player.eye();

        // Safety net: fall below the world -> respawn.
        if (g_player.pos.y < -40.0f) {
            g_player.pos = Vec3(0.5f, 60.0f, 0.5f);
            g_player.vel = Vec3(0, 0, 0);
        }

        // -------- World streaming --------
        int pcx = (int)std::floor(g_player.pos.x / Chunk::CX);
        int pcz = (int)std::floor(g_player.pos.z / Chunk::CZ);
        world.update(pcx, pcz, g_settings.renderDistance);
        world.processMeshQueue();

        // -------- Block interaction --------
        Vec3 fwd = g_cam.front();
        int hx = 0, hy = 0, hz = 0, hface = -1;
        bool hit = raycastBlock(world, g_cam.pos, fwd, hx, hy, hz, hface);

        // Mining: hold LMB to chip a block away. Like Minecraft, progress resets
        // when you release the button or switch to a different block. The time to
        // destroy a block is its hardness times the base breakTime.
        static float mineProgress = 0.0f;
        static int mineX = 0, mineY = 0, mineZ = 0;
        bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool mineable = hit && world.getBlock(hx, hy, hz) != BEDROCK;
        if (leftDown && mineable) {
            if (hx != mineX || hy != mineY || hz != mineZ) {
                mineX = hx; mineY = hy; mineZ = hz;
                mineProgress = 0.0f;
            }
            int hd = blockDef(world.getBlock(hx, hy, hz)).hardness;
            if (hd < 1) hd = 1;
            float breakT = g_settings.breakTime * (float)hd;
            if (breakT < 0.05f) breakT = 0.05f;
            mineProgress += dt;
            if (mineProgress >= breakT) {
                world.setBlock(hx, hy, hz, AIR);
                mineProgress = 0.0f;
            }
        } else {
            mineProgress = 0.0f;
        }

        // Placement: 1/64 s cooldown between placed blocks while holding RMB.
        static float placeTimer = 0.0f;
        bool rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (rightDown) {
            placeTimer += dt;
            if (hit && hface >= 0 && hface < 6 && placeTimer >= 1.0f / 64.0f) {
                static const int FD[6][3] = {
                    {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
                int px = hx + FD[hface][0];
                int py = hy + FD[hface][1];
                int pz = hz + FD[hface][2];
                float hw = Player::WIDTH * 0.5f;
                bool overlaps =
                    (px + 1) > g_player.pos.x - hw && px < g_player.pos.x + hw &&
                    (py + 1) > g_player.pos.y && py < g_player.pos.y + Player::HEIGHT &&
                    (pz + 1) > g_player.pos.z - hw && pz < g_player.pos.z + hw;
                if (!overlaps && world.getBlock(px, py, pz) == AIR) {
                    world.setBlock(px, py, pz, g_placeBlock);
                    placeTimer = 0.0f;
                }
            }
        } else {
            placeTimer = 0.0f;
        }

        // -------- Render --------
        static const Vec3 FOG = Vec3(0.62f, 0.78f, 0.92f);
        static const Vec3 WATER_FOG = Vec3(0.25f, 0.42f, 0.72f);

        FrameState fs;
        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW == 0 || fbH == 0) {
            fs.viewProj = identity();
        } else {
            float aspect = (float)fbW / (float)fbH;
            Mat4 view = g_cam.view();
            Mat4 proj = g_cam.vulkanProjection(aspect);
            fs.viewProj = mat4Mul(proj, view);
        }
        fs.camPos = g_cam.pos;
        fs.fogColor = g_player.inWater ? WATER_FOG : FOG;
        // Fog ends just past the render distance so the world edge fades to sky.
        float edge = (float)g_settings.renderDistance * Chunk::CX;
        fs.fogNear = edge * 0.35f;
        fs.fogFar = edge * 0.92f;
        fs.showBox = hit;
        if (hit) fs.boxPos = Vec3((float)hx, (float)hy, (float)hz);

        // 8-stage crack overlay on the block currently being mined. It samples
        // the crack textures (textures/crackN.png) which use an alpha channel, so
        // only the cracks show over the block. The stage advances with the dig
        // progress and reaches the 8th frame just before the block breaks.
        float crackBreakT = g_settings.breakTime > 0.05f ? g_settings.breakTime : 0.05f;
        if (mineable) {
            int hd = blockDef(world.getBlock(hx, hy, hz)).hardness;
            if (hd < 1) hd = 1;
            crackBreakT *= (float)hd;
        }
        fs.showCrack = mineProgress > 0.0f && mineable;
        fs.crackPos = Vec3((float)hx, (float)hy, (float)hz);
        int crackStage = (int)(mineProgress / crackBreakT * 8.0f);
        fs.crackStage = crackStage > 7 ? 7 : (crackStage < 0 ? 0 : crackStage);

        if (g_takeShot) {
            g_takeShot = false;
            renderer.requestScreenshot();
            renderer.drawFrame(world, fs); // this frame also copies the image out
            renderer.saveScreenshot("/tmp/minkraft_shot.bmp");
        } else {
            renderer.drawFrame(world, fs);
        }

        // -------- Title / perf --------
        fpsFrames++;
        fpsAccum += dt;
        if (fpsAccum >= 0.5) {
            int fps = (int)std::round(fpsFrames / fpsAccum);
            if (getenv("MINKRAFT_PERF")) {
                fprintf(stderr, "[perf] fps=%d chunks=%zu\n", fps, world.chunkCount());
            }
            std::string title = "Minkraft [Vulkan] | seed " + std::to_string(seed) +
                                " | " + std::to_string(fps) + " fps | placing: " +
                                blockDef(g_placeBlock).name;
            glfwSetWindowTitle(window, title.c_str());
            fpsFrames = 0;
            fpsAccum = 0;
        }
    }

    world.clearAll(); // free chunk GPU buffers before the device is destroyed
    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
