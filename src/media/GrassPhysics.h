#ifndef GRASS_PHYSICS_H
#define GRASS_PHYSICS_H

#include <vector>
#include <string>
#include <cstdint>
#include "media/GrassMath3D.h"

enum class PhysBodyType {
    BOX,
    SPHERE,
    CLOTH,
    STATIC
};

struct PhysBody {
    uint32_t id;
    PhysBodyType type;
    GrassVec3 pos;
    GrassVec3 prev_pos;
    GrassVec3 vel;
    GrassVec3 rot; // euler angles
    GrassVec3 size;
    float radius;
    float mass;
    float inv_mass;
    float restitution;
    float friction;
    float break_force;
    bool is_broken;
    std::string material;

    PhysBody() : id(0), type(PhysBodyType::BOX), pos(0,0,0), prev_pos(0,0,0), vel(0,0,0), rot(0,0,0),
                 size(1,1,1), radius(0.5f), mass(1.0f), inv_mass(1.0f), restitution(0.2f), friction(0.4f),
                 break_force(0.0f), is_broken(false), material("wood") {}
};

struct PhysConstraint {
    uint32_t body_a;
    uint32_t body_b;
    float rest_distance;
    float stiffness;
    float break_force;
    bool is_broken;
};

struct PhysRayHit {
    bool hit;
    uint32_t body_id;
    GrassVec3 point;
    GrassVec3 normal;
    float distance;
};

class PhysWorld {
public:
    float gravity;
    std::vector<PhysBody> bodies;
    std::vector<PhysConstraint> constraints;
    uint32_t next_id;

    PhysWorld(float grav_y = -9.81f);
    uint32_t add_box(const GrassVec3& pos, const GrassVec3& size, float mass = 1.0f, const std::string& mat = "wood", float break_f = 0.0f);
    uint32_t add_sphere(const GrassVec3& pos, float r, float mass = 1.0f, const std::string& mat = "steel");
    uint32_t add_cloth(const GrassVec3& pos, float w, float h, int segments);
    void add_link(uint32_t a, uint32_t b, float stiffness, float break_f);
    void step(float dt, int substeps);
    void apply_impulse(uint32_t body_id, const GrassVec3& force);
    PhysRayHit raycast(const GrassVec3& origin, const GrassVec3& dir, float max_dist);
    PhysBody* get_body(uint32_t id);
};

namespace GrassPhysics {
    uint32_t create_world(float gravity_y);
    void step_world(uint32_t world_id, float dt, int substeps);
    uint32_t add_box(uint32_t world_id, float x, float y, float z, float sx, float sy, float sz, float mass = 1.0f, const std::string& mat = "wood", float break_f = 0.0f);
    uint32_t add_sphere(uint32_t world_id, float x, float y, float z, float r, float mass = 1.0f, const std::string& mat = "steel");
    uint32_t add_cloth(uint32_t world_id, float x, float y, float z, float w, float h, int segments);
    void add_link(uint32_t world_id, uint32_t a, uint32_t b, float stiffness, float break_f);
    void apply_impulse(uint32_t world_id, uint32_t body_id, float fx, float fy, float fz);
    PhysRayHit raycast(uint32_t world_id, float ox, float oy, float oz, float dx, float dy, float dz, float max_dist);
    GrassVec3 get_pos(uint32_t world_id, uint32_t body_id);
    GrassVec3 get_rot(uint32_t world_id, uint32_t body_id);
    void set_pos(uint32_t world_id, uint32_t body_id, float x, float y, float z);
    void set_vel(uint32_t world_id, uint32_t body_id, float vx, float vy, float vz);
}

#endif // GRASS_PHYSICS_H
