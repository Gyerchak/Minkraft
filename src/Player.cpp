#include "Player.h"
#include "World.h"
#include <cmath>
#include <algorithm>

static const float GRAVITY = 26.0f;
static const float WALK_SPEED = 4.5f;
static const float SNEAK_SPEED = 1.6f;
static const float JUMP_VEL = 8.5f;

bool Player::collidesAt(World& world, float x, float y, float z) const {
    const float hw = WIDTH * 0.5f;
    int minX = (int)std::floor(x - hw);
    int maxX = (int)std::floor(x + hw);
    int minY = (int)std::floor(y);
    int maxY = (int)std::floor(y + HEIGHT - 0.001f);
    int minZ = (int)std::floor(z - hw);
    int maxZ = (int)std::floor(z + hw);
    for (int by = minY; by <= maxY; by++)
        for (int bz = minZ; bz <= maxZ; bz++)
            for (int bx = minX; bx <= maxX; bx++)
                if (world.isSolid(bx, by, bz))
                    return true;
    return false;
}

Vec3 Player::collideAxis(World& world, Vec3 next, int axis) const {
    if (!collidesAt(world, next.x, next.y, next.z)) return next;

    // Snap back to the face of the block we collided with.
    const float eps = 0.001f;
    if (axis == 0) {
        next.x = pos.x;
        while (collidesAt(world, next.x, next.y, next.z)) {
            next.x += (vel.x > 0) ? -eps : eps;
        }
    } else if (axis == 1) {
        next.y = pos.y;
        while (collidesAt(world, next.x, next.y, next.z)) {
            next.y += (vel.y > 0) ? -eps : eps;
        }
    } else {
        next.z = pos.z;
        while (collidesAt(world, next.x, next.y, next.z)) {
            next.z += (vel.z > 0) ? -eps : eps;
        }
    }
    return next;
}

void Player::update(World& world, float dt,
                    bool fwd, bool back, bool left, bool right,
                    bool jump, bool sneak) {
    (void)fwd; (void)back; (void)left; (void)right;

    int midY = (int)std::floor(pos.y + HEIGHT * 0.5f);
    inWater = world.getBlock((int)std::floor(pos.x), midY, (int)std::floor(pos.z)) == WATER;

    // Cap horizontal speed (movement applied in main.cpp via camera vectors).
    float speed = inWater ? 2.2f : (sneak ? SNEAK_SPEED : WALK_SPEED);
    float hSpeed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
    if (hSpeed > speed) {
        float k = speed / hSpeed;
        vel.x *= k;
        vel.z *= k;
    }

    if (inWater) {
        vel.y -= 18.0f * dt;
        vel.y = std::max(vel.y, -4.0f);
        if (jump) vel.y = 4.5f;
    } else {
        vel.y -= GRAVITY * dt;
        if (jump && onGround) vel.y = JUMP_VEL;
    }
    if (vel.y < -55.0f) vel.y = -55.0f;

    Vec3 next = pos;
    next.x += vel.x * dt;
    next = collideAxis(world, next, 0);
    next.y += vel.y * dt;
    next = collideAxis(world, next, 1);
    next.z += vel.z * dt;
    next = collideAxis(world, next, 2);

    // Ground/ceiling contact: collideAxis snaps the axis back to pos exactly.
    bool yCollided = (next.y == pos.y) && std::abs(vel.y) > 1e-5f;
    if (yCollided) {
        onGround = vel.y <= 0.0f;
        vel.y = 0;
    } else {
        onGround = false;
    }

    if (next.x == pos.x) vel.x = 0;
    if (next.z == pos.z) vel.z = 0;

    pos = next;
}
