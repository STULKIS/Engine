// Derecho Engine - view frustum extraction and culling primitives.
#pragma once

#include "math/Math.h"

namespace derecho::scene {

struct AABB {
    vec3 min = vec3(0.0f);
    vec3 max = vec3(0.0f);

    vec3 Center() const { return (min + max) * 0.5f; }
    vec3 Extents() const { return (max - min) * 0.5f; }
};

struct Sphere {
    vec3 center = vec3(0.0f);
    float radius = 0.0f;
};

class Frustum {
public:
    // Extracts the six planes from a view-projection matrix (Gribb/Hartmann).
    void Extract(const mat4& viewProjection);

    bool ContainsSphere(const vec3& center, float radius) const;
    bool ContainsAABB(const AABB& box) const;
    bool ContainsPoint(const vec3& point) const;

    // Distance from a point to the near plane; negative means outside.
    float NearDistance(const vec3& point) const;

    const vec4& Plane(int index) const { return m_planes[index]; }

    static Frustum FromViewProjection(const mat4& viewProjection) {
        Frustum f;
        f.Extract(viewProjection);
        return f;
    }

private:
    // Plane layout: xyz = inward normal, w = distance along normal.
    vec4 m_planes[6]{};
};

} // namespace derecho::scene
