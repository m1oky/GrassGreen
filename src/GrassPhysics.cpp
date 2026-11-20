#include "GrassPhysics.h"
#include <map>
#include <cmath>
#include <algorithm>

static std::map<uint32_t, PhysWorld> g_worlds;
static uint32_t g_next_world_id = 1;

PhysWorld::PhysWorld(float grav_y) : gravity(grav_y), next_id(1) {}

uint32_t PhysWorld::add_box(const GrassVec3& pos, const GrassVec3& size, const std::string& mat, float break_f) {
    PhysBody b;
    b.id = next_id++;
    b.type = (mat == "static") ? PhysBodyType::STATIC : PhysBodyType::BOX;
    b.pos = pos;
    b.prev_pos = pos;
    b.size = size;
    b.material = mat;
    b.break_force = break_f;

    if (b.type == PhysBodyType::STATIC) {
        b.mass = 0.0f;
        b.inv_mass = 0.0f;
    } else {
        b.mass = size.x * size.y * size.z;
        if (mat == "steel")  { b.mass *= 7.8f; b.restitution = 0.1f; }
        else if (mat == "wood")   { b.mass *= 0.6f; b.restitution = 0.3f; }
        else if (mat == "rubber") { b.mass *= 1.1f; b.restitution = 0.85f; }
        else if (mat == "glass")  { b.mass *= 2.5f; b.restitution = 0.05f; }
        b.inv_mass = 1.0f / (b.mass > 0.001f ? b.mass : 1.0f);
    }

    bodies.push_back(b);
    return b.id;
}

uint32_t PhysWorld::add_sphere(const GrassVec3& pos, float r, const std::string& mat) {
    PhysBody b;
    b.id = next_id++;
    b.type = (mat == "static") ? PhysBodyType::STATIC : PhysBodyType::SPHERE;
    b.pos = pos;
    b.prev_pos = pos;
    b.radius = r;
    b.size = GrassVec3(r * 2.0f, r * 2.0f, r * 2.0f);
    b.material = mat;
    b.break_force = 0.0f;

    if (b.type == PhysBodyType::STATIC) {
        b.mass = 0.0f;
        b.inv_mass = 0.0f;
    } else {
        b.mass = 4.188f * r * r * r;
        if (mat == "steel")  b.restitution = 0.2f;
        else if (mat == "rubber") b.restitution = 0.85f;
        b.inv_mass = 1.0f / (b.mass > 0.001f ? b.mass : 1.0f);
    }

    bodies.push_back(b);
    return b.id;
}

uint32_t PhysWorld::add_cloth(const GrassVec3& pos, float w, float h, int segments) {
    if (segments < 2) segments = 2;
    float dx = w / (float)(segments - 1);
    float dy = h / (float)(segments - 1);

    uint32_t first_id = 0;
    std::vector<uint32_t> grid_ids;
    grid_ids.resize(segments * segments);

    for (int j = 0; j < segments; j++) {
        for (int i = 0; i < segments; i++) {
            GrassVec3 p(pos.x + i * dx, pos.y - j * dy, pos.z);
            PhysBody b;
            b.id = next_id++;
            if (first_id == 0) first_id = b.id;
            b.type = PhysBodyType::CLOTH;
            b.pos = p;
            b.prev_pos = p;
            b.radius = 0.05f;
            b.mass = (j == 0) ? 0.0f : 0.1f;
            b.inv_mass = (j == 0) ? 0.0f : 10.0f;
            bodies.push_back(b);
            grid_ids[j * segments + i] = b.id;
        }
    }

    // Link horizontal and vertical neighbors
    for (int j = 0; j < segments; j++) {
        for (int i = 0; i < segments; i++) {
            uint32_t curr = grid_ids[j * segments + i];
            if (i + 1 < segments) {
                uint32_t right = grid_ids[j * segments + (i + 1)];
                add_link(curr, right, 0.95f, 0.0f);
            }
            if (j + 1 < segments) {
                uint32_t down = grid_ids[(j + 1) * segments + i];
                add_link(curr, down, 0.95f, 0.0f);
            }
        }
    }

    return first_id;
}

void PhysWorld::add_link(uint32_t a, uint32_t b, float stiffness, float break_f) {
    PhysBody* ba = get_body(a);
    PhysBody* bb = get_body(b);
    if (!ba || !bb) return;

    PhysConstraint c;
    c.body_a = a;
    c.body_b = b;
    c.rest_distance = GrassMath3D::length(GrassMath3D::sub(ba->pos, bb->pos));
    c.stiffness = stiffness;
    c.break_force = break_f;
    c.is_broken = false;
    constraints.push_back(c);
}

void PhysWorld::apply_impulse(uint32_t body_id, const GrassVec3& force) {
    PhysBody* b = get_body(body_id);
    if (!b || b->inv_mass <= 0.0f) return;
    b->vel.x += force.x * b->inv_mass;
    b->vel.y += force.y * b->inv_mass;
    b->vel.z += force.z * b->inv_mass;

    float fmag = GrassMath3D::length(force);
    if (b->break_force > 0.0f && fmag > b->break_force) {
        b->is_broken = true;
    }
}

PhysBody* PhysWorld::get_body(uint32_t id) {
    for (auto& b : bodies) {
        if (b.id == id) return &b;
    }
    return nullptr;
}

void PhysWorld::step(float dt, int substeps) {
    if (substeps <= 0) substeps = 1;
    float sdt = dt / (float)substeps;

    for (int step_idx = 0; step_idx < substeps; step_idx++) {
        // 1. Predict positions
        for (auto& b : bodies) {
            if (b.inv_mass <= 0.0f || b.is_broken) continue;
            b.vel.y += gravity * sdt;
            b.prev_pos = b.pos;
            b.pos.x += b.vel.x * sdt;
            b.pos.y += b.vel.y * sdt;
            b.pos.z += b.vel.z * sdt;
        }

        // 2. Solve constraints (XPBD distance constraints)
        for (auto& c : constraints) {
            if (c.is_broken) continue;
            PhysBody* ba = get_body(c.body_a);
            PhysBody* bb = get_body(c.body_b);
            if (!ba || !bb) continue;

            GrassVec3 delta = GrassMath3D::sub(bb->pos, ba->pos);
            float dist = GrassMath3D::length(delta);
            if (dist < 0.0001f) continue;

            float diff = (dist - c.rest_distance) / dist;
            float total_inv = ba->inv_mass + bb->inv_mass;
            if (total_inv <= 0.0f) continue;

            if (c.break_force > 0.0f && std::fabs(dist - c.rest_distance) * c.stiffness > c.break_force) {
                c.is_broken = true;
                continue;
            }

            GrassVec3 corr = GrassMath3D::mul_scalar(delta, diff * c.stiffness / total_inv);
            if (ba->inv_mass > 0.0f) {
                ba->pos = GrassMath3D::add(ba->pos, GrassMath3D::mul_scalar(corr, ba->inv_mass));
            }
            if (bb->inv_mass > 0.0f) {
                bb->pos = GrassMath3D::sub(bb->pos, GrassMath3D::mul_scalar(corr, bb->inv_mass));
            }
        }

        // 3. Environment & Pairwise collision resolution
        for (size_t i = 0; i < bodies.size(); i++) {
            PhysBody& b1 = bodies[i];
            if (b1.is_broken) continue;

            // Ground floor plane at y = 0
            float bottom_y = (b1.type == PhysBodyType::SPHERE) ? (b1.pos.y - b1.radius) : (b1.pos.y - b1.size.y * 0.5f);
            if (bottom_y < 0.0f && b1.inv_mass > 0.0f) {
                float pen = -bottom_y;
                b1.pos.y += pen;
                b1.vel.y = -b1.vel.y * b1.restitution;
                b1.vel.x *= (1.0f - b1.friction);
                b1.vel.z *= (1.0f - b1.friction);
            }

            // Body vs body sphere-approx collision
            for (size_t j = i + 1; j < bodies.size(); j++) {
                PhysBody& b2 = bodies[j];
                if (b2.is_broken) continue;
                if (b1.inv_mass <= 0.0f && b2.inv_mass <= 0.0f) continue;

                GrassVec3 d = GrassMath3D::sub(b2.pos, b1.pos);
                float dist = GrassMath3D::length(d);
                float min_dist = (b1.type == PhysBodyType::SPHERE ? b1.radius : b1.size.x * 0.5f) +
                                 (b2.type == PhysBodyType::SPHERE ? b2.radius : b2.size.x * 0.5f);

                if (dist < min_dist && dist > 0.0001f) {
                    float pen = min_dist - dist;
                    GrassVec3 n = GrassMath3D::normalize(d);
                    float total_inv = b1.inv_mass + b2.inv_mass;
                    if (total_inv > 0.0f) {
                        if (b1.inv_mass > 0.0f) b1.pos = GrassMath3D::sub(b1.pos, GrassMath3D::mul_scalar(n, pen * (b1.inv_mass / total_inv)));
                        if (b2.inv_mass > 0.0f) b2.pos = GrassMath3D::add(b2.pos, GrassMath3D::mul_scalar(n, pen * (b2.inv_mass / total_inv)));
                    }
                }
            }
        }

        // 4. Update velocities from position change
        for (auto& b : bodies) {
            if (b.inv_mass <= 0.0f || b.is_broken) continue;
            b.vel = GrassMath3D::mul_scalar(GrassMath3D::sub(b.pos, b.prev_pos), 1.0f / sdt);
            // Rotational update from linear velocity
            b.rot.x += b.vel.z * sdt * 0.5f;
            b.rot.z -= b.vel.x * sdt * 0.5f;
        }
    }
}

PhysRayHit PhysWorld::raycast(const GrassVec3& origin, const GrassVec3& dir, float max_dist) {
    PhysRayHit best;
    best.hit = false;
    best.distance = max_dist;
    best.body_id = 0;

    GrassVec3 ndir = GrassMath3D::normalize(dir);

    for (const auto& b : bodies) {
        if (b.is_broken) continue;
        GrassVec3 oc = GrassMath3D::sub(b.pos, origin);
        float tca = GrassMath3D::dot(oc, ndir);
        if (tca < 0.0f) continue;
        float d2 = GrassMath3D::dot(oc, oc) - tca * tca;
        float r = (b.type == PhysBodyType::SPHERE) ? b.radius : (b.size.x * 0.5f);
        if (d2 > r * r) continue;
        float thc = std::sqrt(r * r - d2);
        float t0 = tca - thc;
        if (t0 > 0.0f && t0 < best.distance) {
            best.hit = true;
            best.distance = t0;
            best.body_id = b.id;
            best.point = GrassMath3D::add(origin, GrassMath3D::mul_scalar(ndir, t0));
            best.normal = GrassMath3D::normalize(GrassMath3D::sub(best.point, b.pos));
        }
    }
    return best;
}

namespace GrassPhysics {

uint32_t create_world(float gravity_y) {
    uint32_t id = g_next_world_id++;
    g_worlds.emplace(id, PhysWorld(gravity_y));
    return id;
}

void step_world(uint32_t world_id, float dt, int substeps) {
    auto it = g_worlds.find(world_id);
    if (it != g_worlds.end()) {
        it->second.step(dt, substeps);
    }
}

uint32_t add_box(uint32_t world_id, float x, float y, float z, float sx, float sy, float sz, const std::string& mat, float break_f) {
    auto it = g_worlds.find(world_id);
    if (it == g_worlds.end()) return 0;
    return it->second.add_box(GrassVec3(x, y, z), GrassVec3(sx, sy, sz), mat, break_f);
}

uint32_t add_sphere(uint32_t world_id, float x, float y, float z, float r, const std::string& mat) {
    auto it = g_worlds.find(world_id);
    if (it == g_worlds.end()) return 0;
    return it->second.add_sphere(GrassVec3(x, y, z), r, mat);
}

uint32_t add_cloth(uint32_t world_id, float x, float y, float z, float w, float h, int segments) {
    auto it = g_worlds.find(world_id);
    if (it == g_worlds.end()) return 0;
    return it->second.add_cloth(GrassVec3(x, y, z), w, h, segments);
}

void add_link(uint32_t world_id, uint32_t a, uint32_t b, float stiffness, float break_f) {
    auto it = g_worlds.find(world_id);
    if (it != g_worlds.end()) {
        it->second.add_link(a, b, stiffness, break_f);
    }
}

void apply_impulse(uint32_t world_id, uint32_t body_id, float fx, float fy, float fz) {
    auto it = g_worlds.find(world_id);
    if (it != g_worlds.end()) {
        it->second.apply_impulse(body_id, GrassVec3(fx, fy, fz));
    }
}

PhysRayHit raycast(uint32_t world_id, float ox, float oy, float oz, float dx, float dy, float dz, float max_dist) {
    auto it = g_worlds.find(world_id);
    if (it == g_worlds.end()) return PhysRayHit{false, 0, GrassVec3(), GrassVec3(), 0.0f};
    return it->second.raycast(GrassVec3(ox, oy, oz), GrassVec3(dx, dy, dz), max_dist);
}

GrassVec3 get_pos(uint32_t world_id, uint32_t body_id) {
    auto it = g_worlds.find(world_id);
    if (it == g_worlds.end()) return GrassVec3();
    PhysBody* b = it->second.get_body(body_id);
    return b ? b->pos : GrassVec3();
}

GrassVec3 get_rot(uint32_t world_id, uint32_t body_id) {
    auto it = g_worlds.find(world_id);
    if (it == g_worlds.end()) return GrassVec3();
    PhysBody* b = it->second.get_body(body_id);
    return b ? b->rot : GrassVec3();
}

void set_pos(uint32_t world_id, uint32_t body_id, float x, float y, float z) {
    auto it = g_worlds.find(world_id);
    if (it != g_worlds.end()) {
        PhysBody* b = it->second.get_body(body_id);
        if (b) {
            b->pos = GrassVec3(x, y, z);
            b->prev_pos = b->pos;
            b->vel = GrassVec3(0, 0, 0);
            b->rot = GrassVec3(0, 0, 0);
        }
    }
}

void set_vel(uint32_t world_id, uint32_t body_id, float vx, float vy, float vz) {
    auto it = g_worlds.find(world_id);
    if (it != g_worlds.end()) {
        PhysBody* b = it->second.get_body(body_id);
        if (b) {
            b->vel = GrassVec3(vx, vy, vz);
            b->prev_pos = GrassMath3D::sub(b->pos, GrassMath3D::mul_scalar(b->vel, 0.016f));
        }
    }
}

}
