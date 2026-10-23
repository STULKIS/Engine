#include "game/Player.h"

#include "core/Input.h"
#include "scene/Terrain.h"

#include <algorithm>
#include <cmath>

namespace frontier::game {

void Player::Spawn(const scene::Terrain& terrain, const vec3& position) {
    m_position = position;
    const float ground = terrain.HeightAt(position.x, position.z);
    m_position.y = std::max(position.y, ground);
    m_velocity = vec3(0.0f);
    m_grounded = true;
    m_swimming = false;
    m_jumpCooldown = 0.0f;
    UpdateCameraTransform(&terrain);
}

void Player::SetThirdPerson(bool enabled) {
    m_settings.thirdPerson = enabled;
    UpdateCameraTransform(nullptr);
}

void Player::SetThirdPersonDistance(float metres) {
    m_settings.thirdPersonDistance = std::clamp(metres, 1.5f, 12.0f);
}

void Player::ApplyMouseLook(core::Input& input) {
    if (!m_captured) {
        return;
    }
    float dx = 0.0f;
    float dy = 0.0f;
    input.ConsumeMouseDelta(dx, dy);
    const float pitchDelta = (m_settings.invertY ? dy : -dy);
    m_camera.Rotate(dx, pitchDelta);
}

void Player::Update(float deltaSeconds, const core::Input& input, const scene::Terrain& terrain) {
    deltaSeconds = std::clamp(deltaSeconds, 0.0f, 0.1f);
    m_jumpCooldown = std::max(0.0f, m_jumpCooldown - deltaSeconds);

    const bool controlling = m_captured;
    m_sprinting = controlling && input.IsActionDown(core::Action::Sprint);
    const bool walking = controlling && input.IsActionDown(core::Action::Walk);

    vec3 forward = m_camera.Forward();
    forward.y = 0.0f;
    if (glm::length(forward) < 1e-4f) {
        forward = vec3(0.0f, 0.0f, -1.0f);
    }
    forward = glm::normalize(forward);
    const vec3 right = m_camera.Right();

    vec3 wish = controlling ? (forward * input.MoveAxisY() + right * input.MoveAxisX()) : vec3(0.0f);
    if (glm::length(wish) > 1.0f) {
        wish = glm::normalize(wish);
    }

    const float waterSurface = terrain.WaterLevel();
    const float depthHere = terrain.WaterDepthAt(m_position.x, m_position.z);
    m_swimming = depthHere > 1.1f && m_position.y < waterSurface - 0.15f;

    float targetSpeed = m_settings.walkSpeed;
    if (m_sprinting) {
        targetSpeed *= m_settings.sprintMultiplier;
    }
    if (walking) {
        targetSpeed *= m_settings.crouchMultiplier;
    }
    if (m_swimming) {
        targetSpeed = m_settings.swimSpeed;
    }

    const vec3 targetHorizontal = wish * targetSpeed;
    const float blendRate = m_swimming ? 4.0f : (m_grounded ? 14.0f : 3.5f);
    const float blend = 1.0f - std::exp(-blendRate * deltaSeconds);
    m_velocity.x += (targetHorizontal.x - m_velocity.x) * blend;
    m_velocity.z += (targetHorizontal.z - m_velocity.z) * blend;

    if (!m_swimming) {
        m_velocity.y += m_settings.gravity * deltaSeconds;
    } else {
        // Buoyancy: drift toward a stable floating eye height above the surface.
        const float targetY = waterSurface - 1.35f;
        m_velocity.y += (targetY - m_position.y) * 3.2f * deltaSeconds;
        m_velocity.y *= std::exp(-2.4f * deltaSeconds);
        if (controlling && input.IsActionDown(core::Action::Jump)) {
            m_velocity.y += 7.5f * deltaSeconds;
        }
    }

    if (controlling && !m_swimming && m_grounded && m_jumpCooldown <= 0.0f &&
        input.WasActionPressed(core::Action::Jump)) {
        m_velocity.y = m_settings.jumpVelocity;
        m_grounded = false;
        m_jumpCooldown = 0.12f;
    }

    // Horizontal move with slope blocking and simple axis sliding.
    const vec3 previous = m_position;
    vec3 candidate = m_position + vec3(m_velocity.x, 0.0f, m_velocity.z) * deltaSeconds;
    const float groundAhead = terrain.HeightAt(candidate.x, candidate.z);
    const float stepUp = groundAhead - m_position.y;
    const bool tooSteep = !m_swimming &&
                          terrain.NormalAt(candidate.x, candidate.z).y < m_settings.maxSlopeCos &&
                          groundAhead > m_position.y + 0.08f;

    if (m_swimming || (!tooSteep && stepUp <= m_settings.maxStepHeight)) {
        m_position.x = candidate.x;
        m_position.z = candidate.z;
    } else {
        // Try each axis independently so the player slides along steep walls.
        const vec3 tryX(m_position.x + m_velocity.x * deltaSeconds, m_position.y, m_position.z);
        const bool steepX = terrain.NormalAt(tryX.x, tryX.z).y < m_settings.maxSlopeCos &&
                            terrain.HeightAt(tryX.x, tryX.z) > m_position.y + 0.08f;
        if (!steepX && terrain.HeightAt(tryX.x, tryX.z) - m_position.y <= m_settings.maxStepHeight) {
            m_position.x = tryX.x;
        } else {
            m_velocity.x = 0.0f;
        }
        const vec3 tryZ(m_position.x, m_position.y, m_position.z + m_velocity.z * deltaSeconds);
        const bool steepZ = terrain.NormalAt(tryZ.x, tryZ.z).y < m_settings.maxSlopeCos &&
                            terrain.HeightAt(tryZ.x, tryZ.z) > m_position.y + 0.08f;
        if (!steepZ && terrain.HeightAt(tryZ.x, tryZ.z) - m_position.y <= m_settings.maxStepHeight) {
            m_position.z = tryZ.z;
        } else {
            m_velocity.z = 0.0f;
        }
    }

    // Vertical integration and ground resolution.
    m_position.y += m_velocity.y * deltaSeconds;
    const float ground = terrain.HeightAt(m_position.x, m_position.z);

    if (m_swimming) {
        const float floatingY = waterSurface - 1.2f;
        if (m_position.y < ground) {
            m_position.y = ground;
            m_velocity.y = std::max(0.0f, m_velocity.y);
        }
        if (m_position.y > floatingY) {
            m_position.y = std::max(floatingY, waterSurface - 0.6f);
            if (m_velocity.y > 0.0f) {
                m_velocity.y = 0.0f;
            }
        }
        m_grounded = false;
    } else if (m_position.y <= ground) {
        m_position.y = ground;
        if (m_velocity.y < 0.0f) {
            m_velocity.y = 0.0f;
        }
        m_grounded = true;
    } else if (m_grounded && m_velocity.y <= 0.0f &&
               m_position.y - ground <= m_settings.maxStepHeight) {
        // Walk downhill without skipping: stick to the surface within step range.
        m_position.y = ground;
        m_velocity.y = 0.0f;
    } else {
        m_grounded = false;
    }

    // Reject absurd step-ups caused by fast movement across cliffs.
    if (m_position.y - previous.y > m_settings.maxStepHeight + 1.0f && !m_swimming) {
        m_position = previous;
        m_velocity.x = m_velocity.z = 0.0f;
    }

    // Keep the player inside the generated map.
    const float half = terrain.Size() * 0.5f - 3.0f;
    m_position.x = Clamp(m_position.x, -half, half);
    m_position.z = Clamp(m_position.z, -half, half);

    m_camera.UpdateBob(deltaSeconds, HorizontalSpeed(), m_velocity.y, m_grounded);
    UpdateCameraTransform(&terrain);
}

void Player::UpdateCameraTransform(const scene::Terrain* terrain) {
    const vec3 head = m_position + vec3(0.0f, m_settings.eyeHeight, 0.0f);
    if (!m_settings.thirdPerson) {
        m_camera.SetPosition(head);
        return;
    }

    vec3 forward = m_camera.Forward();
    if (glm::length(forward) < 1e-4f) {
        forward = vec3(0.0f, 0.0f, -1.0f);
    }
    // Pull back along the view direction, then bias over the right shoulder so
    // the character does not sit dead centre.
    vec3 position = head + vec3(0.0f, 0.25f, 0.0f) - forward * m_settings.thirdPersonDistance +
                    m_camera.Right() * 0.45f;

    if (terrain) {
        // Keep the camera out of the ground and above the water surface.
        const float ground = terrain->HeightAt(position.x, position.z);
        position.y = std::max(position.y, ground + 0.45f);
    }
    m_camera.SetPosition(position);
}

bool Player::CanStandAt(const scene::Terrain& terrain, const vec3& position) const {
    const vec3 normal = terrain.NormalAt(position.x, position.z);
    const float ground = terrain.HeightAt(position.x, position.z);
    if (terrain.WaterDepthAt(position.x, position.z) > 0.6f) {
        return true;
    }
    return normal.y >= m_settings.maxSlopeCos && ground <= position.y + m_settings.maxStepHeight;
}

} // namespace frontier::game
