#ifndef VOXEL_CHARACTER_CONTROLLER_H
#define VOXEL_CHARACTER_CONTROLLER_H

#include <functional>

namespace voxel {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct CharacterConfig {
    float radius = 0.3f;
    float height = 1.8f;
    float skin = 0.01f;
    float step_height = 0.2f;
    float jump_clearance = 0.2f;
    float block_size = 0.6f;
    float gravity = -9.81f;
};

struct CharacterInput {
    float accel_x = 0.0f;
    float accel_z = 0.0f;
    bool jump = false;
    float jump_speed = 5.5f;
};

class CharacterController {
public:
    using SolidQuery = std::function<bool(int ix, int iy, int iz)>;

    explicit CharacterController(const CharacterConfig& config);

    void setSolidQuery(SolidQuery query);
    void setPosition(const Vec3& pos);
    void setVelocity(const Vec3& vel);
    void setGravity(float gravity);

    const Vec3& position() const;
    const Vec3& velocity() const;
    bool isGrounded() const;

    void update(float dt, const CharacterInput& input);

private:
    void fixedUpdate(float dt, const CharacterInput& input);
    bool moveAxis(float delta, int axis, bool allow_step);
    bool overlapsSolid(const Vec3& min, const Vec3& max) const;
    void getAabb(Vec3* out_min, Vec3* out_max) const;
    bool hasHeadroom(float clearance) const;

    CharacterConfig config_;
    SolidQuery is_solid_;
    Vec3 position_;
    Vec3 velocity_;
    bool grounded_ = false;
    float accumulator_ = 0.0f;
};

} // namespace voxel

#endif