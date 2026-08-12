#pragma once
#include "Math.h"

class World;

class Player {
public:
    static constexpr float WIDTH = 0.6f;
    static constexpr float HEIGHT = 1.8f;
    static constexpr float EYE = 1.62f;

    Vec3 pos;          // feet position (bottom-center)
    Vec3 vel;
    bool onGround = false;

    Vec3 eye() const { return {pos.x, pos.y + EYE, pos.z}; }

    // Applies gravity, movement keys and collision against the world.
    void update(World& world, float dt,
                bool fwd, bool back, bool left, bool right,
                bool jump, bool sneak);

    bool inWater = false;

private:
    bool collidesAt(World& world, float x, float y, float z) const;
    Vec3 collideAxis(World& world, Vec3 next, int axis) const;
};
