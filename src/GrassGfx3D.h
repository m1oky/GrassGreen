#ifndef GRASS_GFX3D_H
#define GRASS_GFX3D_H

#include <vector>
#include <string>
#include <cstdint>
#include "GrassMath3D.h"

struct GfxVertex {
    GrassVec3 pos;
    GrassVec3 normal;
    float u, v;
    uint32_t color;
};

struct GfxMesh {
    uint32_t id;
    std::vector<GfxVertex> vertices;
    std::vector<uint32_t> indices;
    GrassVec3 pos;
    GrassVec3 rot;
    GrassVec3 scale;
    GrassMat4 model_matrix;
    bool has_custom_matrix = false;
    uint32_t color;
    std::string texture_path;
};

struct GfxLight {
    bool is_directional;
    GrassVec3 pos_or_dir;
    GrassVec3 color;
    float intensity_or_range;
};

namespace GrassGfx3D {
    bool init_window(int w, int h, const std::string& title);
    bool is_open();
    void clear(float r, float g, float b);
    void present();
    float dt();
    void set_camera(const GrassVec3& pos, const GrassVec3& target, float fov_deg);

    uint32_t create_cube(float size);
    uint32_t create_sphere(float radius, int segments);
    uint32_t create_plane(float w, float d);
    uint32_t load_obj(const std::string& path);
    void set_texture(uint32_t mesh_id, const std::string& path);
    void set_color(uint32_t mesh_id, float r, float g, float b);
    void transform(uint32_t mesh_id, const GrassVec3& pos, const GrassVec3& rot, const GrassVec3& scale);
    void transform_mat(uint32_t mesh_id, const GrassMat4& mat);
    void draw(uint32_t mesh_id);

    void set_dir_light(const GrassVec3& dir, float r, float g, float b, float intensity);
    void set_point_light(const GrassVec3& pos, float r, float g, float b, float range);
    void set_fog(float r, float g, float b, float density);
    void set_skybox(uint32_t top_rgb, uint32_t bottom_rgb);
    void bind_framebuffer(void* addr, int w, int h, int pitch);
}

#endif // GRASS_GFX3D_H
