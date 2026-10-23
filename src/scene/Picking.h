// Derecho Engine - picking and projection helpers shared by the editor and the
// headless tests, so the viewport maths can be verified without a GPU.
#pragma once

#include "math/Math.h"

namespace derecho::scene {

struct Ray {
    vec3 origin{0.0f};
    vec3 direction{0.0f, 0.0f, -1.0f};
};

// Slab test. `distance` is the near hit along the ray (0 when the ray starts
// inside the box). Returns false when the box is behind the ray.
bool RayIntersectsAabb(const Ray& ray, const vec3& boundsMin, const vec3& boundsMax,
                       float& distance);

// Projects a world point with a view-projection matrix. Returns false when the
// point is behind the camera, in which case `ndc` is untouched.
bool ProjectToNdc(const mat4& viewProj, const vec3& world, vec2& ndc);

} // namespace derecho::scene
