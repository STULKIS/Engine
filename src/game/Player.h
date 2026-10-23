// Frontier Engine - first-person character controller with terrain collision.
#pragma once

#include "scene/Camera.h"

namespace frontier::core { class Input; }
namespace frontier::scene { class Terrain; }

namespace frontier::game {

struct PlayerSettings {
    float eyeHeight = 1.72f;
    float radius = 0.34f;
    float walkSpeed = 6.5f;
    float sprintMultiplier = 1.9f;
    float crouchMultiplier = 0.5f;
    float jumpVelocity = 6.0f;
    float gravity = -20.0f;
    float acceleration = 48.0f;
    float airControl = 0.25f;
    float maxStepHeight = 0.65f;
    float maxSlopeCos = 0.55f;   // cos of the steepest walkable slope
    float swimSpeed = 3.2f;
    float mouseSensitivity = 0.11f;
    bool invertY = false;
    bool thirdPerson = false;
    float thirdPersonDistance = 4.5f;
};

class Player {
public:
    Player() = default;

    void Spawn(const scene::Terrain& terrain, const vec3& position);
    void SetThirdPerson(bool enabled);
    bool ThirdPerson() const { return m_settings.thirdPerson; }
    void SetThirdPersonDistance(float metres);
    void Update(float deltaSeconds, const core::Input& input, const scene::Terrain& terrain);
    void ApplyMouseLook(core::Input& input);

    scene::Camera& Camera() { return m_camera; }
    const scene::Camera& Camera() const { return m_camera; }

    const vec3& Position() const { return m_position; }
    const vec3& Velocity() const { return m_velocity; }
    bool IsGrounded() const { return m_grounded; }
    bool IsSwimming() const { return m_swimming; }
    bool IsSprinting() const { return m_sprinting; }
    float HorizontalSpeed() const { return glm::length(vec2(m_velocity.x, m_velocity.z)); }
    float EyeHeight() const { return m_settings.eyeHeight; }
    bool CameraRelativeMouse() const { return m_captured; }
    void SetCaptured(bool captured) { m_captured = captured; }

    // Yaw in degrees, for orienting the player's visible character model.
    float YawDegrees() const { return glm::degrees(m_camera.Yaw()); }

    PlayerSettings& Settings() { return m_settings; }
    const PlayerSettings& Settings() const { return m_settings; }

private:
    void UpdateCameraTransform(const scene::Terrain* terrain);
    bool CanStandAt(const scene::Terrain& terrain, const vec3& position) const;

    PlayerSettings m_settings;
    scene::Camera m_camera;
    vec3 m_position = vec3(0.0f);
    vec3 m_velocity = vec3(0.0f);
    bool m_grounded = false;
    bool m_swimming = false;
    bool m_sprinting = false;
    bool m_captured = false;
    float m_jumpCooldown = 0.0f;
};

} // namespace frontier::game
