#include "voxel_character_controller.h"

#include <algorithm>
#include <cmath>

namespace voxel {

namespace {
constexpr float kFixedDt = 1.0f / 60.0f;
constexpr float kMinDt = 0.000001f;

int FloorToInt(float value) {
    return static_cast<int>(std::floor(value));
}
} // namespace

CharacterController::CharacterController(const CharacterConfig& config)
    : config_(config) {}

void CharacterController::setSolidQuery(SolidQuery query) {
    is_solid_ = std::move(query);
}

void CharacterController::setPosition(const Vec3& pos) {
    position_ = pos;
}

void CharacterController::setVelocity(const Vec3& vel) {
    velocity_ = vel;
}

void CharacterController::setGravity(float gravity) {
    config_.gravity = gravity;
}

void CharacterController::setGravityEnabled(bool enabled) {
    gravity_enabled_ = enabled;
    if (gravity_enabled_) {
        return;
    }
    velocity_.y = 0.0f;
    grounded_ = false;
}

void CharacterController::setCollisionEnabled(bool enabled) {
    collision_enabled_ = enabled;
}

bool CharacterController::gravityEnabled() const {
    return gravity_enabled_;
}

bool CharacterController::collisionEnabled() const {
    return collision_enabled_;
}

const Vec3& CharacterController::position() const {
    return position_;
}

const Vec3& CharacterController::velocity() const {
    return velocity_;
}

bool CharacterController::isGrounded() const {
    return grounded_;
}

void CharacterController::update(float dt, const CharacterInput& input) {
    accumulator_ += std::max(dt, 0.0f);
    while (accumulator_ >= kFixedDt) {
        fixedUpdate(kFixedDt, input);
        accumulator_ -= kFixedDt;
    }
}

void CharacterController::fixedUpdate(float dt, const CharacterInput& input) {
    bool was_grounded = grounded_;
    grounded_ = false;
    velocity_.x += input.accel_x * dt;
    velocity_.y += input.accel_y * dt;
    velocity_.z += input.accel_z * dt;
    if (gravity_enabled_)
        velocity_.y += config_.gravity * dt;
    if (input.jump && was_grounded && hasHeadroom(config_.jump_clearance)) {
        velocity_.y = input.jump_speed;
    }

    Vec3 delta{velocity_.x * dt, velocity_.y * dt, velocity_.z * dt};
    moveAxis(delta.x, 0, true);
    moveAxis(delta.y, 1, false);
    moveAxis(delta.z, 2, true);
}

bool CharacterController::moveAxis(float delta, int axis, bool allow_step) {
    if (std::abs(delta) < kMinDt)
        return false;

    Vec3 original = position_;
    if (axis == 0)
        position_.x += delta;
    else if (axis == 1)
        position_.y += delta;
    else
        position_.z += delta;

    Vec3 min_aabb;
    Vec3 max_aabb;
    getAabb(&min_aabb, &max_aabb);
    if (!overlapsSolid(min_aabb, max_aabb))
        return true;

    position_ = original;
    float direction = (delta > 0.0f) ? 1.0f : -1.0f;
    float moved = 0.0f;
    float max_move = std::abs(delta);
    float step = std::max(config_.skin, 0.001f);
    for (float traveled = 0.0f; traveled < max_move; traveled += step) {
        moved = std::min(max_move, traveled + step);
        if (axis == 0)
            position_.x = original.x + direction * moved;
        else if (axis == 1)
            position_.y = original.y + direction * moved;
        else
            position_.z = original.z + direction * moved;
        getAabb(&min_aabb, &max_aabb);
        if (overlapsSolid(min_aabb, max_aabb)) {
            moved = std::max(0.0f, moved - step);
            break;
        }
    }
    if (axis == 0)
        position_.x = original.x + direction * moved;
    else if (axis == 1)
        position_.y = original.y + direction * moved;
    else
        position_.z = original.z + direction * moved;

    if (axis == 1 && direction < 0.0f)
        grounded_ = true;

    if (axis == 0)
        velocity_.x = 0.0f;
    else if (axis == 1)
        velocity_.y = 0.0f;
    else
        velocity_.z = 0.0f;

    if (allow_step && config_.step_height > 0.0f && axis != 1) {
        Vec3 before_step = position_;
        position_.y += config_.step_height;
        getAabb(&min_aabb, &max_aabb);
        if (!overlapsSolid(min_aabb, max_aabb)) {
            if (axis == 0)
                position_.x = original.x + direction * max_move;
            else
                position_.z = original.z + direction * max_move;
            getAabb(&min_aabb, &max_aabb);
            if (!overlapsSolid(min_aabb, max_aabb))
                return true;
        }
        position_ = before_step;
    }
    return false;
}

bool CharacterController::overlapsSolid(const Vec3& min, const Vec3& max) const {
    if (!collision_enabled_)
        return false;
    if (!is_solid_)
        return false;
    const int min_x = FloorToInt(min.x / config_.block_size);
    const int min_y = FloorToInt(min.y / config_.block_size);
    const int min_z = FloorToInt(min.z / config_.block_size);
    const int max_x = FloorToInt(max.x / config_.block_size);
    const int max_y = FloorToInt(max.y / config_.block_size);
    const int max_z = FloorToInt(max.z / config_.block_size);
    for (int z = min_z; z <= max_z; ++z) {
        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                if (is_solid_(x, y, z))
                    return true;
            }
        }
    }
    return false;
}

void CharacterController::getAabb(Vec3* out_min, Vec3* out_max) const {
    const float half_height = std::max(config_.height * 0.5f - config_.radius, 0.0f);
    const float radius = config_.radius + config_.skin;
    Vec3 center = position_;
    out_min->x = center.x - radius;
    out_min->y = center.y - half_height - radius;
    out_min->z = center.z - radius;
    out_max->x = center.x + radius;
    out_max->y = center.y + half_height + radius;
    out_max->z = center.z + radius;
}

bool CharacterController::hasHeadroom(float clearance) const {
    if (clearance <= 0.0f)
        return true;
    Vec3 min_aabb;
    Vec3 max_aabb;
    getAabb(&min_aabb, &max_aabb);
    min_aabb.y += clearance;
    max_aabb.y += clearance;
    return !overlapsSolid(min_aabb, max_aabb);
}

} // namespace voxel