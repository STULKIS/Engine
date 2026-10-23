// Derecho Engine - picking and projection helpers.
#include "scene/Picking.h"

#include <algorithm>
#include <cmath>

namespace derecho::scene {

bool RayIntersectsAabb(const Ray& ray, const vec3& boundsMin, const vec3& boundsMax,
                       float& distance) {
    float tMin = 0.0f;
    float tMax = 1e30f;
    for (int axis = 0; axis < 3; ++axis) {
        const float origin = ray.origin[axis];
        const float direction = ray.direction[axis];
        if (std::fabs(direction) < 1e-6f) {
            if (origin < boundsMin[axis] || origin > boundsMax[axis]) {
                return false;
            }
            continue;
        }
        float t1 = (boundsMin[axis] - origin) / direction;
        float t2 = (boundsMax[axis] - origin) / direction;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMin > tMax) {
            return false;
        }
    }
    if (tMax <= 0.0f) {
        return false;
    }
    distance = tMin;
    return true;
}

bool ProjectToNdc(const mat4& viewProj, const vec3& world, vec2& ndc) {
    const vec4 clip = viewProj * vec4(world, 1.0f);
    if (clip.w <= 0.0001f) {
        return false;
    }
    const vec3 normalized = vec3(clip) / clip.w;
    ndc = vec2(normalized.x, normalized.y);
    return true;
}

} // namespace derecho::scene
