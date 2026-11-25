#include "media/GrassPhysics.h"
#include <map>
#include <cmath>
#include <algorithm>

static std::map<uint32_t, PhysWorld> g_worlds;
static uint32_t g_next_world_id = 1;

PhysWorld::PhysWorld(float grav_y) : gravity(grav_y), next_id(1) {}

uint32_t PhysWorld::add_box(const GrassVec3& pos, const GrassVec3& size, float mass, const std::string& mat, float break_f) {
    PhysBody b;
    b.id = next_id++;
    b.pos = pos;
    b.prev_pos = pos;
    b.size = size;
    b.radius = size.x * 0.5f;
    b.material = mat;
    b.break_force = break_f;

    if (mass <= 0.0f || mat == "static") {
        b.type = PhysBodyType::STATIC;
        b.mass = 0.0f;
        b.inv_mass = 0.0f;
    } else {
        b.type = PhysBodyType::BOX;
        b.mass = mass;
        b.inv_mass = 1.0f / mass;
        b.restitution = 0.1f;
        b.friction = 0.65f;
        if (mat == "steel")  { b.restitution = 0.1f; b.friction = 0.5f; }
        else if (mat == "wood")   { b.restitution = 0.15f; b.friction = 0.7f; }
        else if (mat == "rubber") { b.restitution = 0.85f; b.friction = 0.8f; }
        else if (mat == "glass")  { b.restitution = 0.05f; b.friction = 0.3f; }
    }

    bodies.push_back(b);
    return b.id;
}

uint32_t PhysWorld::add_sphere(const GrassVec3& pos, float r, float mass, const std::string& mat) {
    PhysBody b;
    b.id = next_id++;
    b.pos = pos;
    b.prev_pos = pos;
    b.radius = r;
    b.size = GrassVec3(r * 2.0f, r * 2.0f, r * 2.0f);
    b.material = mat;
    b.break_force = 0.0f;

    if (mass <= 0.0f || mat == "static") {
        b.type = PhysBodyType::STATIC;
        b.mass = 0.0f;
        b.inv_mass = 0.0f;
    } else {
        b.type = PhysBodyType::SPHERE;
        b.mass = mass;
        b.inv_mass = 1.0f / mass;
        b.restitution = 0.2f;
        b.friction = 0.5f;
        if (mat == "steel")  { b.restitution = 0.2f; b.friction = 0.4f; }
        else if (mat == "rubber") { b.restitution = 0.85f; b.friction = 0.8f; }
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

struct ContactManifold {
    bool hit;
    GrassVec3 normal; // Points from body 1 to body 2
    float penetration;
    GrassVec3 point; // World contact point
};

static void get_body_axes(const PhysBody& b, GrassVec3 axes[3]) {
    if (std::fabs(b.rot.x) < 0.001f && std::fabs(b.rot.y) < 0.001f && std::fabs(b.rot.z) < 0.001f) {
        axes[0] = GrassVec3(1.0f, 0.0f, 0.0f);
        axes[1] = GrassVec3(0.0f, 1.0f, 0.0f);
        axes[2] = GrassVec3(0.0f, 0.0f, 1.0f);
        return;
    }
    float cx = std::cos(b.rot.x), sx = std::sin(b.rot.x);
    float cy = std::cos(b.rot.y), sy = std::sin(b.rot.y);
    float cz = std::cos(b.rot.z), sz = std::sin(b.rot.z);

    axes[0] = GrassVec3(cy * cz + sy * sx * sz, cx * sz, -sy * cz + cy * sx * sz);
    axes[1] = GrassVec3(-cy * sz + sy * sx * cz, cx * cz, sy * sz + cy * sx * cz);
    axes[2] = GrassVec3(sy * cx, -sx, cy * cx);
}

static bool test_sat_axis(const GrassVec3& axis, const GrassVec3& p,
                          const GrassVec3 a_axes[3], const GrassVec3& a_h,
                          const GrassVec3 b_axes[3], const GrassVec3& b_h,
                          float& min_pen, GrassVec3& best_normal)
{
    float len2 = GrassMath3D::dot(axis, axis);
    if (len2 < 1e-6f) return true;

    GrassVec3 L = GrassMath3D::mul_scalar(axis, 1.0f / std::sqrt(len2));

    float rA = a_h.x * std::fabs(GrassMath3D::dot(a_axes[0], L)) +
               a_h.y * std::fabs(GrassMath3D::dot(a_axes[1], L)) +
               a_h.z * std::fabs(GrassMath3D::dot(a_axes[2], L));

    float rB = b_h.x * std::fabs(GrassMath3D::dot(b_axes[0], L)) +
               b_h.y * std::fabs(GrassMath3D::dot(b_axes[1], L)) +
               b_h.z * std::fabs(GrassMath3D::dot(b_axes[2], L));

    float d = std::fabs(GrassMath3D::dot(p, L));
    float pen = (rA + rB) - d;
    if (pen <= 0.0f) {
        return false;
    }

    if (pen < min_pen) {
        min_pen = pen;
        if (GrassMath3D::dot(p, L) < 0.0f) {
            L = GrassMath3D::mul_scalar(L, -1.0f);
        }
        best_normal = L;
    }
    return true;
}

static ContactManifold collide_box_box(const PhysBody& b1, const PhysBody& b2) {
    ContactManifold cm;
    cm.hit = false;
    cm.penetration = 1e9f;
    cm.normal = GrassVec3(0, 1, 0);

    GrassVec3 a_axes[3], b_axes[3];
    get_body_axes(b1, a_axes);
    get_body_axes(b2, b_axes);

    GrassVec3 a_h = GrassMath3D::mul_scalar(b1.size, 0.5f);
    GrassVec3 b_h = GrassMath3D::mul_scalar(b2.size, 0.5f);
    GrassVec3 p = GrassMath3D::sub(b2.pos, b1.pos);

    for (int i = 0; i < 3; i++) {
        if (!test_sat_axis(a_axes[i], p, a_axes, a_h, b_axes, b_h, cm.penetration, cm.normal)) return cm;
    }
    for (int i = 0; i < 3; i++) {
        if (!test_sat_axis(b_axes[i], p, a_axes, a_h, b_axes, b_h, cm.penetration, cm.normal)) return cm;
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            GrassVec3 cross_ax = GrassMath3D::cross(a_axes[i], b_axes[j]);
            if (!test_sat_axis(cross_ax, p, a_axes, a_h, b_axes, b_h, cm.penetration, cm.normal)) return cm;
        }
    }

    cm.hit = true;
    cm.point = GrassMath3D::add(b1.pos, GrassMath3D::mul_scalar(p, 0.5f));
    return cm;
}

static ContactManifold collide_sphere_box(const PhysBody& b_sphere, const PhysBody& b_box) {
    ContactManifold cm;
    cm.hit = false;
    cm.penetration = 0.0f;
    cm.normal = GrassVec3(0, 1, 0);

    GrassVec3 b_axes[3];
    get_body_axes(b_box, b_axes);
    GrassVec3 b_h = GrassMath3D::mul_scalar(b_box.size, 0.5f);
    GrassVec3 d = GrassMath3D::sub(b_sphere.pos, b_box.pos);

    float lx = GrassMath3D::dot(d, b_axes[0]);
    float ly = GrassMath3D::dot(d, b_axes[1]);
    float lz = GrassMath3D::dot(d, b_axes[2]);

    float cx = std::clamp(lx, -b_h.x, b_h.x);
    float cy = std::clamp(ly, -b_h.y, b_h.y);
    float cz = std::clamp(lz, -b_h.z, b_h.z);

    GrassVec3 cp = GrassMath3D::add(
        b_box.pos,
        GrassMath3D::add(
            GrassMath3D::mul_scalar(b_axes[0], cx),
            GrassMath3D::add(
                GrassMath3D::mul_scalar(b_axes[1], cy),
                GrassMath3D::mul_scalar(b_axes[2], cz)
            )
        )
    );

    GrassVec3 diff = GrassMath3D::sub(b_sphere.pos, cp);
    float dist = GrassMath3D::length(diff);

    if (dist > 0.0001f) {
        if (dist < b_sphere.radius) {
            cm.hit = true;
            cm.penetration = b_sphere.radius - dist;
            cm.normal = GrassMath3D::mul_scalar(diff, 1.0f / dist); // Points box -> sphere
            cm.point = cp;
        }
    } else {
        float dx = b_h.x - std::fabs(lx);
        float dy = b_h.y - std::fabs(ly);
        float dz = b_h.z - std::fabs(lz);
        cm.hit = true;
        cm.point = cp;
        if (dx <= dy && dx <= dz) {
            cm.penetration = b_sphere.radius + dx;
            cm.normal = GrassMath3D::mul_scalar(b_axes[0], lx >= 0.0f ? 1.0f : -1.0f);
        } else if (dy <= dx && dy <= dz) {
            cm.penetration = b_sphere.radius + dy;
            cm.normal = GrassMath3D::mul_scalar(b_axes[1], ly >= 0.0f ? 1.0f : -1.0f);
        } else {
            cm.penetration = b_sphere.radius + dz;
            cm.normal = GrassMath3D::mul_scalar(b_axes[2], lz >= 0.0f ? 1.0f : -1.0f);
        }
    }
    return cm;
}

void PhysWorld::step(float dt, int substeps) {
    if (substeps < 8) substeps = 8;
    float sdt = dt / (float)substeps;

    for (int step_idx = 0; step_idx < substeps; step_idx++) {
        // 1. Predict positions with gravity and light damping
        for (auto& b : bodies) {
            if (b.inv_mass <= 0.0f || b.is_broken) continue;
            b.vel.y += gravity * sdt;
            b.vel.x *= (1.0f - 0.02f * sdt);
            b.vel.z *= (1.0f - 0.02f * sdt);

            b.prev_pos = b.pos;
            b.pos.x += b.vel.x * sdt;
            b.pos.y += b.vel.y * sdt;
            b.pos.z += b.vel.z * sdt;
        }

        // 2. Solve distance constraints (XPBD)
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

        // 3. Two solver iterations for rigid contact resolution and stacking stiffness
        for (int iter = 0; iter < 2; iter++) {
            for (size_t i = 0; i < bodies.size(); i++) {
                PhysBody& b1 = bodies[i];
                if (b1.is_broken) continue;

                // Ground floor plane at y = 0
                if (b1.inv_mass > 0.0f) {
                    float bottom_y = (b1.type == PhysBodyType::SPHERE) ? (b1.pos.y - b1.radius) : (b1.pos.y - b1.size.y * 0.5f);
                    if (bottom_y < 0.0f) {
                        float pen = -bottom_y;
                        b1.pos.y += pen;
                        b1.vel.y = -b1.vel.y * b1.restitution;
                        float frict = std::clamp(b1.friction, 0.0f, 1.0f);
                        b1.vel.x *= (1.0f - frict * 0.3f);
                        b1.vel.z *= (1.0f - frict * 0.3f);
                        if (std::fabs(b1.vel.x) < 0.05f) { b1.vel.x = 0.0f; b1.prev_pos.x = b1.pos.x; }
                        if (std::fabs(b1.vel.z) < 0.05f) { b1.vel.z = 0.0f; b1.prev_pos.z = b1.pos.z; }
                        b1.rot.x *= 0.95f;
                        b1.rot.z *= 0.95f;
                    }
                }

                // Pairwise Body-vs-Body SAT / Closest-Point collisions
                for (size_t j = i + 1; j < bodies.size(); j++) {
                    PhysBody& b2 = bodies[j];
                    if (b2.is_broken) continue;
                    if (b1.inv_mass <= 0.0f && b2.inv_mass <= 0.0f) continue;

                    bool is_b1_box = (b1.type == PhysBodyType::BOX || b1.type == PhysBodyType::STATIC);
                    bool is_b2_box = (b2.type == PhysBodyType::BOX || b2.type == PhysBodyType::STATIC);

                    ContactManifold cm;
                    cm.hit = false;

                    if (is_b1_box && is_b2_box) {
                        cm = collide_box_box(b1, b2);
                    } else if (b1.type == PhysBodyType::SPHERE && is_b2_box) {
                        ContactManifold temp = collide_sphere_box(b1, b2);
                        if (temp.hit) {
                            cm = temp;
                            cm.normal = GrassMath3D::mul_scalar(cm.normal, -1.0f); // b1 -> b2
                        }
                    } else if (is_b1_box && b2.type == PhysBodyType::SPHERE) {
                        cm = collide_sphere_box(b2, b1); // b1 (box) -> b2 (sphere)
                    } else if (b1.type == PhysBodyType::SPHERE && b2.type == PhysBodyType::SPHERE) {
                        GrassVec3 d = GrassMath3D::sub(b2.pos, b1.pos);
                        float dist = GrassMath3D::length(d);
                        float min_dist = b1.radius + b2.radius;
                        if (dist < min_dist && dist > 0.0001f) {
                            cm.hit = true;
                            cm.penetration = min_dist - dist;
                            cm.normal = GrassMath3D::mul_scalar(d, 1.0f / dist);
                            cm.point = GrassMath3D::add(b1.pos, GrassMath3D::mul_scalar(d, 0.5f));
                        }
                    }

                    if (cm.hit && cm.penetration > 0.0f) {
                        float total_inv = b1.inv_mass + b2.inv_mass;
                        if (total_inv > 0.0f) {
                            float p1 = (b1.inv_mass / total_inv) * cm.penetration;
                            float p2 = (b2.inv_mass / total_inv) * cm.penetration;

                            if (b1.inv_mass > 0.0f) b1.pos = GrassMath3D::sub(b1.pos, GrassMath3D::mul_scalar(cm.normal, p1));
                            if (b2.inv_mass > 0.0f) b2.pos = GrassMath3D::add(b2.pos, GrassMath3D::mul_scalar(cm.normal, p2));

                            GrassVec3 rel_vel = GrassMath3D::sub(b2.vel, b1.vel);
                            float vn = GrassMath3D::dot(rel_vel, cm.normal);

                            if (vn < 0.0f) {
                                float rest = 0.5f * (b1.restitution + b2.restitution);
                                float imp = -(1.0f + rest) * vn / total_inv;
                                GrassVec3 imp_v = GrassMath3D::mul_scalar(cm.normal, imp);

                                if (b1.inv_mass > 0.0f) b1.vel = GrassMath3D::sub(b1.vel, GrassMath3D::mul_scalar(imp_v, b1.inv_mass));
                                if (b2.inv_mass > 0.0f) b2.vel = GrassMath3D::add(b2.vel, GrassMath3D::mul_scalar(imp_v, b2.inv_mass));

                                // Off-center angular torque
                                if (b1.inv_mass > 0.0f && b1.type == PhysBodyType::BOX) {
                                    GrassVec3 r1 = GrassMath3D::sub(cm.point, b1.pos);
                                    GrassVec3 t1 = GrassMath3D::cross(r1, GrassMath3D::mul_scalar(cm.normal, -imp));
                                    b1.rot.x += t1.x * 0.05f * sdt;
                                    b1.rot.y += t1.y * 0.05f * sdt;
                                    b1.rot.z += t1.z * 0.05f * sdt;
                                }
                                if (b2.inv_mass > 0.0f && b2.type == PhysBodyType::BOX) {
                                    GrassVec3 r2 = GrassMath3D::sub(cm.point, b2.pos);
                                    GrassVec3 t2 = GrassMath3D::cross(r2, GrassMath3D::mul_scalar(cm.normal, imp));
                                    b2.rot.x += t2.x * 0.05f * sdt;
                                    b2.rot.y += t2.y * 0.05f * sdt;
                                    b2.rot.z += t2.z * 0.05f * sdt;
                                }
                            }

                            // Dynamic and static friction between contacting faces
                            float frict = std::sqrt(b1.friction * b2.friction);
                            GrassVec3 vt = GrassMath3D::sub(rel_vel, GrassMath3D::mul_scalar(cm.normal, GrassMath3D::dot(rel_vel, cm.normal)));
                            float vt_len = GrassMath3D::length(vt);
                            if (vt_len > 0.0001f) {
                                float f_damp = std::min(vt_len, frict * 0.8f);
                                GrassVec3 f_vec = GrassMath3D::mul_scalar(vt, f_damp / vt_len);
                                if (b1.inv_mass > 0.0f) b1.vel = GrassMath3D::add(b1.vel, GrassMath3D::mul_scalar(f_vec, b1.inv_mass / total_inv));
                                if (b2.inv_mass > 0.0f) b2.vel = GrassMath3D::sub(b2.vel, GrassMath3D::mul_scalar(f_vec, b2.inv_mass / total_inv));
                            } else {
                                if (b1.inv_mass > 0.0f) { b1.vel.x *= 0.85f; b1.vel.z *= 0.85f; }
                                if (b2.inv_mass > 0.0f) { b2.vel.x *= 0.85f; b2.vel.z *= 0.85f; }
                            }
                        }
                    }
                }
            }
        }

        // 4. Update velocities from position change
        for (auto& b : bodies) {
            if (b.inv_mass <= 0.0f || b.is_broken) continue;
            b.vel = GrassMath3D::mul_scalar(GrassMath3D::sub(b.pos, b.prev_pos), 1.0f / sdt);
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

uint32_t add_box(uint32_t world_id, float x, float y, float z, float sx, float sy, float sz, float mass, const std::string& mat, float break_f) {
    auto it = g_worlds.find(world_id);
    if (it == g_worlds.end()) return 0;
    return it->second.add_box(GrassVec3(x, y, z), GrassVec3(sx, sy, sz), mass, mat, break_f);
}

uint32_t add_sphere(uint32_t world_id, float x, float y, float z, float r, float mass, const std::string& mat) {
    auto it = g_worlds.find(world_id);
    if (it == g_worlds.end()) return 0;
    return it->second.add_sphere(GrassVec3(x, y, z), r, mass, mat);
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
