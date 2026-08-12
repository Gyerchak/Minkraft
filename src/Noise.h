#pragma once
#include <cstdint>
#include <cmath>

// Deterministic hash-based value noise.

inline uint32_t hash3(int x, int y, int z, uint32_t seed) {
    uint32_t h = seed ^ (uint32_t)x * 374761393u ^ (uint32_t)y * 668265263u ^ (uint32_t)z * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = (h ^ (h >> 16));
    return h;
}

inline float hash3f(int x, int y, int z, uint32_t seed) {
    return (hash3(x, y, z, seed) & 0x00FFFFFF) / (float)0x01000000;
}

inline float smoothstep01(float t) {
    return t * t * (3.0f - 2.0f * t);
}

inline float valueNoise2D(float x, float y, uint32_t seed) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    float fx = x - x0, fy = y - y0;
    float sx = smoothstep01(fx), sy = smoothstep01(fy);
    float n00 = hash3f(x0, y0, 0, seed);
    float n10 = hash3f(x0 + 1, y0, 0, seed);
    float n01 = hash3f(x0, y0 + 1, 0, seed);
    float n11 = hash3f(x0 + 1, y0 + 1, 0, seed);
    float a = n00 + (n10 - n00) * sx;
    float b = n01 + (n11 - n01) * sx;
    return a + (b - a) * sy; // [0,1]
}

inline float valueNoise3D(float x, float y, float z, uint32_t seed) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y), z0 = (int)std::floor(z);
    float fx = x - x0, fy = y - y0, fz = z - z0;
    float sx = smoothstep01(fx), sy = smoothstep01(fy), sz = smoothstep01(fz);
    float c000 = hash3f(x0, y0, z0, seed);
    float c100 = hash3f(x0 + 1, y0, z0, seed);
    float c010 = hash3f(x0, y0 + 1, z0, seed);
    float c110 = hash3f(x0 + 1, y0 + 1, z0, seed);
    float c001 = hash3f(x0, y0, z0 + 1, seed);
    float c101 = hash3f(x0 + 1, y0, z0 + 1, seed);
    float c011 = hash3f(x0, y0 + 1, z0 + 1, seed);
    float c111 = hash3f(x0 + 1, y0 + 1, z0 + 1, seed);
    float a00 = c000 + (c100 - c000) * sx;
    float a10 = c010 + (c110 - c010) * sx;
    float a01 = c001 + (c101 - c001) * sx;
    float a11 = c011 + (c111 - c011) * sx;
    float b0 = a00 + (a10 - a00) * sy;
    float b1 = a01 + (a11 - a01) * sy;
    return b0 + (b1 - b0) * sz; // [0,1]
}

// Fractal Brownian motion. Returns roughly in [0,1].
inline float fbm2D(float x, float y, uint32_t seed, int octaves, float persistence = 0.5f, float lacunarity = 2.0f) {
    float sum = 0, amp = 1, freq = 1, norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += amp * valueNoise2D(x * freq, y * freq, seed + i * 131u);
        norm += amp;
        amp *= persistence;
        freq *= lacunarity;
    }
    return sum / norm;
}

inline float fbm3D(float x, float y, float z, uint32_t seed, int octaves, float persistence = 0.5f, float lacunarity = 2.0f) {
    float sum = 0, amp = 1, freq = 1, norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += amp * valueNoise3D(x * freq, y * freq, z * freq, seed + i * 131u);
        norm += amp;
        amp *= persistence;
        freq *= lacunarity;
    }
    return sum / norm;
}
