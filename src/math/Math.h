// Frontier Engine - math aliases and helpers built on GLM.
#pragma once

// windows.h defines min()/max() as macros unless NOMINMAX is set before the
// first include, which breaks every std::max/std::min below (and in GLM).
// The build also defines these on the command line for all Windows targets.
#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX 1
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN 1
#  endif
#endif

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace frontier {

using glm::mat3;
using glm::mat4;
using glm::quat;
using glm::vec2;
using glm::vec3;
using glm::vec4;

constexpr float kPi = glm::pi<float>();
constexpr float kTwoPi = glm::two_pi<float>();
constexpr float kEpsilon = 1e-6f;

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Saturate(float v) { return Clamp(v, 0.0f, 1.0f); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline vec3 Lerp(const vec3& a, const vec3& b, float t) { return a + (b - a) * t; }
inline float SmoothStep(float edge0, float edge1, float x) {
    const float t = Saturate((x - edge0) / std::max(edge1 - edge0, kEpsilon));
    return t * t * (3.0f - 2.0f * t);
}
inline float Radians(float degrees) { return degrees * (kPi / 180.0f); }
inline float Degrees(float radians) { return radians * (180.0f / kPi); }

// Wraps an angle into [0, 2pi).
inline float WrapAngle(float radians) {
    radians = std::fmod(radians, kTwoPi);
    if (radians < 0.0f) {
        radians += kTwoPi;
    }
    return radians;
}

// Shortest signed angular difference (b - a) in radians.
inline float AngleDelta(float a, float b) {
    float d = std::fmod(b - a + kPi, kTwoPi);
    if (d < 0.0f) {
        d += kTwoPi;
    }
    return d - kPi;
}

// Builds a rotation matrix from yaw (around +Y) and pitch (around local X).
inline mat4 YawPitchMatrix(float yaw, float pitch) {
    mat4 m = glm::rotate(mat4(1.0f), yaw, vec3(0, 1, 0));
    m = glm::rotate(m, pitch, vec3(1, 0, 0));
    return m;
}

// Right-handed look-at / perspective helpers (GLM defaults match OpenGL clip space).
inline mat4 Perspective(float fovYRadians, float aspect, float nearPlane, float farPlane) {
    return glm::perspective(fovYRadians, aspect, nearPlane, farPlane);
}

// Matrix using only the rotational part of a transform (for normals).
inline mat3 NormalMatrix(const mat4& model) {
    return glm::transpose(glm::inverse(mat3(model)));
}

// Packs a linear [0,1] colour into 24-bit RGB hex for documentation/UI.
inline unsigned PackRGB(const vec3& c) {
    const unsigned r = static_cast<unsigned>(Saturate(c.r) * 255.0f + 0.5f);
    const unsigned g = static_cast<unsigned>(Saturate(c.g) * 255.0f + 0.5f);
    const unsigned b = static_cast<unsigned>(Saturate(c.b) * 255.0f + 0.5f);
    return (r << 16) | (g << 8) | b;
}

// Henyey-Greenstein phase function used by the sky and sun scattering.
inline float HenyeyGreenstein(float cosTheta, float g) {
    const float g2 = g * g;
    const float denom = std::pow(std::max(1.0f + g2 - 2.0f * g * cosTheta, 1e-4f), 1.5f);
    return (1.0f - g2) / (4.0f * kPi * denom);
}

// Moves `current` toward `target` at `rate` units per second, frame-rate independent.
inline float MoveTowards(float current, float target, float maxDelta) {
    const float d = target - current;
    if (std::fabs(d) <= maxDelta) {
        return target;
    }
    return current + (d > 0.0f ? maxDelta : -maxDelta);
}

inline bool NearlyEqual(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

} // namespace frontier
