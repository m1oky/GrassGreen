#ifndef GRASS_MATH3D_H
#define GRASS_MATH3D_H

#include <cmath>

struct GrassVec2 {
    float x, y;
    GrassVec2() : x(0.0f), y(0.0f) {}
    GrassVec2(float _x, float _y) : x(_x), y(_y) {}
};

struct GrassVec3 {
    float x, y, z;
    GrassVec3() : x(0.0f), y(0.0f), z(0.0f) {}
    GrassVec3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
};

struct GrassVec4 {
    float x, y, z, w;
    GrassVec4() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
    GrassVec4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

struct GrassQuat {
    float x, y, z, w;
    GrassQuat() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
    GrassQuat(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

struct GrassMat4 {
    float m[16];
    GrassMat4();
};

namespace GrassMath3D {
    GrassVec3 add(const GrassVec3& a, const GrassVec3& b);
    GrassVec3 sub(const GrassVec3& a, const GrassVec3& b);
    GrassVec3 mul_scalar(const GrassVec3& a, float s);
    float dot(const GrassVec3& a, const GrassVec3& b);
    GrassVec3 cross(const GrassVec3& a, const GrassVec3& b);
    float length(const GrassVec3& a);
    GrassVec3 normalize(const GrassVec3& a);
    GrassVec3 lerp(const GrassVec3& a, const GrassVec3& b, float t);

    GrassMat4 identity();
    GrassMat4 translate(const GrassVec3& v);
    GrassMat4 rotate(float rad, const GrassVec3& axis);
    GrassMat4 scale(const GrassVec3& v);
    GrassMat4 look_at(const GrassVec3& eye, const GrassVec3& target, const GrassVec3& up);
    GrassMat4 perspective(float fov_rad, float aspect, float near_z, float far_z);
    GrassMat4 multiply(const GrassMat4& a, const GrassMat4& b);
    GrassVec4 transform_vec4(const GrassMat4& m, const GrassVec4& v);
}

#endif // GRASS_MATH3D_H
