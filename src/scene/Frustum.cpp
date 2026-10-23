#include "scene/Frustum.h"

namespace derecho::scene {

void Frustum::Extract(const mat4& vp) {
    // Row vectors of the (column-major) matrix.
    const vec4 r0(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
    const vec4 r1(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
    const vec4 r2(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
    const vec4 r3(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);

    m_planes[0] = r3 + r0; // left
    m_planes[1] = r3 - r0; // right
    m_planes[2] = r3 + r1; // bottom
    m_planes[3] = r3 - r1; // top
    m_planes[4] = r3 + r2; // near
    m_planes[5] = r3 - r2; // far

    for (vec4& plane : m_planes) {
        const float length = glm::length(vec3(plane));
        if (length > 1e-6f) {
            plane /= length;
        }
    }
}

bool Frustum::ContainsSphere(const vec3& center, float radius) const {
    for (const vec4& plane : m_planes) {
        const float distance = glm::dot(vec3(plane), center) + plane.w;
        if (distance < -radius) {
            return false;
        }
    }
    return true;
}

bool Frustum::ContainsAABB(const AABB& box) const {
    const vec3 center = box.Center();
    const vec3 extents = box.Extents();
    for (const vec4& plane : m_planes) {
        const vec3 normal = vec3(plane);
        // Positive vertex: furthest along the plane normal.
        const vec3 positive = center + vec3(normal.x >= 0.0f ? extents.x : -extents.x,
                                           normal.y >= 0.0f ? extents.y : -extents.y,
                                           normal.z >= 0.0f ? extents.z : -extents.z);
        if (glm::dot(normal, positive) + plane.w < 0.0f) {
            return false;
        }
    }
    return true;
}

bool Frustum::ContainsPoint(const vec3& point) const {
    for (const vec4& plane : m_planes) {
        if (glm::dot(vec3(plane), point) + plane.w < 0.0f) {
            return false;
        }
    }
    return true;
}

float Frustum::NearDistance(const vec3& point) const {
    return glm::dot(vec3(m_planes[4]), point) + m_planes[4].w;
}

} // namespace derecho::scene
