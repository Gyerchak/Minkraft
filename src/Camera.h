#pragma once
#include "Math.h"
#include <cmath>

class Camera {
public:
    Vec3 pos;
    float yaw = -90.0f; // degrees
    float pitch = 0.0f;
    float fov = 70.0f;
    float farPlane = 600.0f;

    Vec3 front() const {
        float cy = std::cos(yaw * (float)M_PI / 180.0f);
        float sy = std::sin(yaw * (float)M_PI / 180.0f);
        float cp = std::cos(pitch * (float)M_PI / 180.0f);
        float sp = std::sin(pitch * (float)M_PI / 180.0f);
        return normalize(Vec3(cy * cp, sp, sy * cp));
    }

    Vec3 right() const { return normalize(cross(front(), Vec3(0, 1, 0))); }

    Mat4 view() const {
        return lookAt(pos, pos + front(), Vec3(0, 1, 0));
    }

    Mat4 projection(float aspect) const {
        return perspective(fov * (float)M_PI / 180.0f, aspect, 0.05f, farPlane);
    }

    Mat4 vulkanProjection(float aspect) const {
        return vulkanPerspective(fov * (float)M_PI / 180.0f, aspect, 0.05f, farPlane);
    }

    void rotate(float dYaw, float dPitch) {
        yaw += dYaw;
        pitch += dPitch;
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
    }
};
