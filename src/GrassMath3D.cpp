#include "GrassMath3D.h"

GrassMat4::GrassMat4() {
    for (int i = 0; i < 16; i++) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

namespace GrassMath3D {

GrassVec3 add(const GrassVec3& a, const GrassVec3& b) {
    return GrassVec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

GrassVec3 sub(const GrassVec3& a, const GrassVec3& b) {
    return GrassVec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

GrassVec3 mul_scalar(const GrassVec3& a, float s) {
    return GrassVec3(a.x * s, a.y * s, a.z * s);
}

float dot(const GrassVec3& a, const GrassVec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

GrassVec3 cross(const GrassVec3& a, const GrassVec3& b) {
    return GrassVec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

float length(const GrassVec3& a) {
    return std::sqrt(dot(a, a));
}

GrassVec3 normalize(const GrassVec3& a) {
    float len = length(a);
    if (len <= 0.000001f) return GrassVec3(0, 0, 0);
    float inv = 1.0f / len;
    return GrassVec3(a.x * inv, a.y * inv, a.z * inv);
}

GrassVec3 lerp(const GrassVec3& a, const GrassVec3& b, float t) {
    return GrassVec3(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    );
}

GrassMat4 identity() {
    return GrassMat4();
}

GrassMat4 translate(const GrassVec3& v) {
    GrassMat4 res;
    res.m[12] = v.x;
    res.m[13] = v.y;
    res.m[14] = v.z;
    return res;
}

GrassMat4 rotate(float rad, const GrassVec3& axis) {
    GrassMat4 res;
    GrassVec3 ax = normalize(axis);
    float c = std::cos(rad);
    float s = std::sin(rad);
    float omc = 1.0f - c;

    res.m[0] = ax.x * ax.x * omc + c;
    res.m[1] = ax.x * ax.y * omc + ax.z * s;
    res.m[2] = ax.x * ax.z * omc - ax.y * s;
    res.m[3] = 0.0f;

    res.m[4] = ax.y * ax.x * omc - ax.z * s;
    res.m[5] = ax.y * ax.y * omc + c;
    res.m[6] = ax.y * ax.z * omc + ax.x * s;
    res.m[7] = 0.0f;

    res.m[8]  = ax.z * ax.x * omc + ax.y * s;
    res.m[9]  = ax.z * ax.y * omc - ax.x * s;
    res.m[10] = ax.z * ax.z * omc + c;
    res.m[11] = 0.0f;

    res.m[12] = res.m[13] = res.m[14] = 0.0f;
    res.m[15] = 1.0f;
    return res;
}

GrassMat4 scale(const GrassVec3& v) {
    GrassMat4 res;
    res.m[0]  = v.x;
    res.m[5]  = v.y;
    res.m[10] = v.z;
    res.m[15] = 1.0f;
    return res;
}

GrassMat4 look_at(const GrassVec3& eye, const GrassVec3& target, const GrassVec3& up) {
    GrassVec3 f = normalize(sub(target, eye));
    GrassVec3 s = normalize(cross(f, up));
    GrassVec3 u = cross(s, f);

    GrassMat4 res;
    res.m[0] = s.x;  res.m[4] = s.y;  res.m[8]  = s.z;  res.m[12] = -dot(s, eye);
    res.m[1] = u.x;  res.m[5] = u.y;  res.m[9]  = u.z;  res.m[13] = -dot(u, eye);
    res.m[2] = -f.x; res.m[6] = -f.y; res.m[10] = -f.z; res.m[14] = dot(f, eye);
    res.m[3] = 0.0f; res.m[7] = 0.0f; res.m[11] = 0.0f; res.m[15] = 1.0f;
    return res;
}

GrassMat4 perspective(float fov_rad, float aspect, float near_z, float far_z) {
    GrassMat4 res;
    for (int i = 0; i < 16; i++) res.m[i] = 0.0f;
    float tanHalfFov = std::tan(fov_rad * 0.5f);
    res.m[0]  = 1.0f / (aspect * tanHalfFov);
    res.m[5]  = 1.0f / tanHalfFov;
    res.m[10] = -(far_z + near_z) / (far_z - near_z);
    res.m[11] = -1.0f;
    res.m[14] = -(2.0f * far_z * near_z) / (far_z - near_z);
    return res;
}

GrassMat4 multiply(const GrassMat4& a, const GrassMat4& b) {
    GrassMat4 res;
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            res.m[col * 4 + row] = sum;
        }
    }
    return res;
}

GrassVec4 transform_vec4(const GrassMat4& m, const GrassVec4& v) {
    return GrassVec4(
        m.m[0] * v.x + m.m[4] * v.y + m.m[8]  * v.z + m.m[12] * v.w,
        m.m[1] * v.x + m.m[5] * v.y + m.m[9]  * v.z + m.m[13] * v.w,
        m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w,
        m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w
    );
}

}
