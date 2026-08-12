#pragma once
#include <cmath>

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, const Vec3& a) { return a * s; }
inline Vec3 operator/(const Vec3& a, float s) { return {a.x / s, a.y / s, a.z / s}; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 normalize(const Vec3& a) {
    float l = std::sqrt(dot(a, a));
    return (l > 1e-8f) ? a / l : Vec3(0, 0, 0);
}
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Column-major 4x4 matrix (OpenGL convention).
struct Mat4 {
    float m[16] = {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1};
};

inline Mat4 identity() { return Mat4(); }

inline Mat4 perspective(float fovY, float aspect, float znear, float zfar) {
    Mat4 r;
    float f = 1.0f / std::tan(fovY * 0.5f);
    for (int i = 0; i < 16; i++) r.m[i] = 0;
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    return r;
}

// Vulkan NDC: Y points down and depth maps to [0,1].
// Reversed-Z: near -> 1, far -> 0 (best float precision at distance).
inline Mat4 vulkanPerspective(float fovY, float aspect, float znear, float zfar) {
    Mat4 r;
    float f = 1.0f / std::tan(fovY * 0.5f);
    float d = zfar - znear;
    for (int i = 0; i < 16; i++) r.m[i] = 0;
    r.m[0] = f / aspect;
    r.m[5] = -f;
    r.m[10] = znear / d;
    r.m[11] = -1.0f;
    r.m[14] = (znear * zfar) / d;
    return r;
}

inline Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 r;
    for (int i = 0; i < 16; i++) r.m[i] = 0;
    r.m[0] = s.x; r.m[4] = s.y; r.m[8]  = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    r.m[15] = 1.0f;
    return r;
}

inline Mat4 mat4Mul(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; c++) {
        for (int ro = 0; ro < 4; ro++) {
            float sum = 0;
            for (int k = 0; k < 4; k++)
                sum += a.m[k * 4 + ro] * b.m[c * 4 + k];
            r.m[c * 4 + ro] = sum;
        }
    }
    return r;
}

inline Mat4 translation(const Vec3& t) {
    Mat4 r;
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}
