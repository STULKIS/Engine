// Frontier Engine - first-person camera.
#pragma once

#include "math/Math.h"

namespace frontier::scene {

class Camera {
public:
    Camera() = default;

    void SetPerspective(float fovYDegrees, float aspect, float nearPlane, float farPlane);
    void SetPosition(const vec3& position) { m_position = position; }
    void SetRotation(float yaw, float pitch);
    void LookAt(const vec3& target);

    // Applies a mouse delta in radians (already scaled by sensitivity).
    void Rotate(float deltaYaw, float deltaPitch);
    void SetFov(float degrees);
    void SetAspect(float aspect);

    const vec3& Position() const { return m_position; }
    float Yaw() const { return m_yaw; }
    float Pitch() const { return m_pitch; }
    float Fov() const { return m_fovDegrees; }
    float NearPlane() const { return m_nearPlane; }
    float FarPlane() const { return m_farPlane; }
    float Aspect() const { return m_aspect; }

    vec3 Forward() const;
    vec3 Right() const;
    vec3 Up() const;

    mat4 ViewMatrix() const;
    mat4 ProjectionMatrix() const;
    mat4 ViewProjection() const;
    mat4 ViewProjectionNoTranslation() const;

    // World-space ray from screen normalised device coordinates.
    vec3 RayDirection(float ndcX, float ndcY) const;

    // Applies subtle head bob and landing shake; call once per frame.
    void UpdateBob(float deltaSeconds, float horizontalSpeed, float verticalSpeed, bool grounded);

private:
    vec3 m_position = vec3(0.0f, 20.0f, 0.0f);
    float m_yaw = 0.0f;     // 0 looks toward -Z
    float m_pitch = 0.0f;
    float m_fovDegrees = 68.0f;
    float m_aspect = 16.0f / 9.0f;
    float m_nearPlane = 0.1f;
    float m_farPlane = 900.0f;

    float m_bobPhase = 0.0f;
    float m_bobStrength = 0.0f;
    vec3 m_bobOffset = vec3(0.0f);
};

} // namespace frontier::scene
