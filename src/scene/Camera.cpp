#include "scene/Camera.h"

#include <algorithm>

namespace frontier::scene {

void Camera::SetPerspective(float fovYDegrees, float aspect, float nearPlane, float farPlane) {
    m_fovDegrees = fovYDegrees;
    m_aspect = aspect;
    m_nearPlane = nearPlane;
    m_farPlane = farPlane;
}

void Camera::SetRotation(float yaw, float pitch) {
    m_yaw = WrapAngle(yaw);
    m_pitch = Clamp(pitch, -kPi * 0.4999f, kPi * 0.4999f);
}

void Camera::LookAt(const vec3& target) {
    const vec3 dir = glm::normalize(target - m_position);
    m_pitch = std::asin(Clamp(dir.y, -1.0f, 1.0f));
    m_yaw = std::atan2(dir.x, -dir.z);
}

void Camera::Rotate(float deltaYaw, float deltaPitch) {
    m_yaw = WrapAngle(m_yaw + deltaYaw);
    m_pitch = Clamp(m_pitch + deltaPitch, -kPi * 0.4999f, kPi * 0.4999f);
}

void Camera::SetFov(float degrees) {
    m_fovDegrees = std::clamp(degrees, 25.0f, 120.0f);
}

void Camera::SetAspect(float aspect) {
    m_aspect = aspect > 0.01f ? aspect : 1.0f;
}

vec3 Camera::Forward() const {
    const float cp = std::cos(m_pitch);
    return vec3(cp * std::sin(m_yaw), std::sin(m_pitch), -cp * std::cos(m_yaw));
}

vec3 Camera::Right() const {
    return vec3(std::cos(m_yaw), 0.0f, std::sin(m_yaw));
}

vec3 Camera::Up() const {
    return glm::normalize(glm::cross(Right(), Forward()));
}

mat4 Camera::ViewMatrix() const {
    return glm::lookAt(m_position + m_bobOffset, m_position + m_bobOffset + Forward(), vec3(0, 1, 0));
}

mat4 Camera::ProjectionMatrix() const {
    return Perspective(Radians(m_fovDegrees), m_aspect, m_nearPlane, m_farPlane);
}

mat4 Camera::ViewProjection() const {
    return ProjectionMatrix() * ViewMatrix();
}

mat4 Camera::ViewProjectionNoTranslation() const {
    const mat4 view = glm::lookAt(vec3(0.0f), Forward(), vec3(0, 1, 0));
    return ProjectionMatrix() * view;
}

vec3 Camera::RayDirection(float ndcX, float ndcY) const {
    const mat4 inv = glm::inverse(ViewProjection());
    vec4 farPoint = inv * vec4(ndcX, ndcY, 1.0f, 1.0f);
    farPoint /= farPoint.w;
    return glm::normalize(vec3(farPoint) - m_position);
}

void Camera::UpdateBob(float deltaSeconds, float horizontalSpeed, float verticalSpeed, bool grounded) {
    const float targetStrength = grounded ? Saturate(horizontalSpeed / 8.0f) : 0.0f;
    m_bobStrength = MoveTowards(m_bobStrength, targetStrength, deltaSeconds * 3.0f);

    if (grounded && horizontalSpeed > 0.2f) {
        m_bobPhase += deltaSeconds * (6.5f + horizontalSpeed * 0.45f);
    } else {
        m_bobPhase += deltaSeconds * 1.5f;
    }

    const float vertical = std::sin(m_bobPhase * 2.0f) * 0.022f * m_bobStrength;
    const float lateral = std::sin(m_bobPhase) * 0.018f * m_bobStrength;
    const float airborne = grounded ? 0.0f : Clamp(verticalSpeed * -0.004f, -0.05f, 0.05f);
    m_bobOffset = Right() * lateral + vec3(0.0f, vertical + airborne, 0.0f);
}

} // namespace frontier::scene
