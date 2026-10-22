#include "scene/TimeOfDay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace frontier::scene {

void TimeOfDay::Configure(float startHours, float dayLengthMinutes, bool paused) {
    SetHours(startHours);
    m_dayLengthMinutes = std::max(1.0f, dayLengthMinutes);
    m_paused = paused;
    m_timeScale = 1.0f;
}

void TimeOfDay::Update(float deltaSeconds) {
    if (m_paused || deltaSeconds <= 0.0f) {
        return;
    }
    // One in-game day takes m_dayLengthMinutes of real time.
    const float gameHoursPerSecond = 24.0f / (m_dayLengthMinutes * 60.0f);
    SetHours(m_hours + deltaSeconds * gameHoursPerSecond * m_timeScale);
}

void TimeOfDay::SetHours(float hours) {
    m_hours = std::fmod(hours, 24.0f);
    if (m_hours < 0.0f) {
        m_hours += 24.0f;
    }
}

SunSample TimeOfDay::Sun() const {
    // 06:00 = sunrise (east), 12:00 = high sun, 18:00 = sunset (west).
    const float theta = (m_hours - 6.0f) / 24.0f * kTwoPi;
    // Tilted arc so the sun rises in the east and sets in the west, with a
    // southerly declination for a northern-hemisphere look.
    const float tilt = Radians(28.0f);
    vec3 sunDir(std::cos(theta), std::sin(theta) * std::cos(tilt), std::sin(theta) * std::sin(tilt));
    // Small offset avoids a perfectly overhead sun at noon.
    sunDir.z -= 0.18f;
    sunDir = glm::normalize(sunDir);

    SunSample sample;
    sample.position = sunDir;
    sample.direction = -sunDir;
    sample.elevationDegrees = Degrees(std::asin(Clamp(sunDir.y, -1.0f, 1.0f)));
    sample.azimuthDegrees = Degrees(std::atan2(sunDir.x, -sunDir.z)) + 180.0f;

    const float elevation = sample.elevationDegrees;
    const float daylight = SmoothStep(-6.0f, 4.0f, elevation);

    // Horizon reddening: deep orange at 0 deg, neutral white above ~25 deg.
    const float warmth = 1.0f - SmoothStep(0.0f, 26.0f, elevation);
    const vec3 noonColor(1.0f, 0.97f, 0.92f);
    const vec3 sunsetColor(1.0f, 0.42f, 0.16f);
    sample.color = Lerp(noonColor, sunsetColor, warmth * 0.85f);
    sample.intensity = 3.15f * daylight;

    if (elevation < -4.0f) {
        sample.intensity = 0.0f;
        sample.color = vec3(0.16f, 0.24f, 0.42f);
    }
    return sample;
}

vec3 TimeOfDay::MoonDirection() const {
    const SunSample sun = Sun();
    vec3 moon = -sun.position;
    moon.y = std::fabs(moon.y) * 0.8f + 0.25f;
    return glm::normalize(moon);
}

vec3 TimeOfDay::MoonColor() const {
    return vec3(0.36f, 0.47f, 0.75f);
}

float TimeOfDay::MoonIntensity() const {
    return 0.16f * StarFactor();
}

float TimeOfDay::DaylightFactor() const {
    return SmoothStep(-8.0f, 6.0f, Sun().elevationDegrees);
}

float TimeOfDay::StarFactor() const {
    return 1.0f - SmoothStep(-12.0f, -2.0f, Sun().elevationDegrees);
}

vec3 TimeOfDay::AmbientSkyColor(const vec3& sunColor) const {
    const float day = DaylightFactor();
    const vec3 dayAmbient(0.42f, 0.52f, 0.68f);
    const vec3 nightAmbient(0.035f, 0.055f, 0.10f);
    const vec3 duskAmbient(0.28f, 0.25f, 0.35f);
    const float dusk = 1.0f - SmoothStep(0.0f, 0.45f, std::fabs(DaylightFactor() - 0.5f) * 2.0f);
    vec3 ambient = Lerp(nightAmbient, dayAmbient, day);
    ambient = Lerp(ambient, duskAmbient * glm::length(sunColor) * 0.5f, dusk * 0.4f);
    return ambient;
}

vec3 TimeOfDay::AmbientGroundColor(const vec3& sunColor) const {
    const float day = DaylightFactor();
    const vec3 dayGround(0.16f, 0.17f, 0.12f);
    const vec3 nightGround(0.012f, 0.016f, 0.02f);
    // Warm the bounce light slightly with the sun's colour at low elevations.
    const vec3 bounce = glm::normalize(sunColor + vec3(0.001f)) * 0.05f;
    return Lerp(nightGround, dayGround + bounce, day);
}

vec3 TimeOfDay::FogColor(const vec3& sunColor) const {
    const float day = DaylightFactor();
    const vec3 dayFog(0.62f, 0.72f, 0.84f);
    const vec3 nightFog(0.035f, 0.05f, 0.09f);
    const vec3 horizonWarm(0.72f, 0.5f, 0.34f);
    const float warmth = 1.0f - SmoothStep(0.0f, 18.0f, Sun().elevationDegrees);
    vec3 fog = Lerp(nightFog, dayFog, day);
    fog = Lerp(fog, horizonWarm * (0.6f + 0.4f * day), warmth * 0.55f);
    // Tint the fog very slightly toward the sun's colour for cohesion.
    const vec3 sunTint = glm::normalize(sunColor + vec3(0.001f));
    fog *= 0.94f + 0.12f * sunTint;
    return fog;
}

std::string TimeOfDay::PhaseName() const {
    const float elevation = Sun().elevationDegrees;
    if (elevation < -12.0f) return "Night";
    if (elevation < -3.0f) return "Astronomical twilight";
    if (elevation < 0.0f) return "Civil twilight";
    if (elevation < 10.0f) return m_hours < 12.0f ? "Sunrise" : "Sunset";
    if (elevation < 35.0f) return m_hours < 12.0f ? "Morning" : "Afternoon";
    return "Midday";
}

std::string TimeOfDay::ClockString() const {
    int totalMinutes = static_cast<int>(m_hours * 60.0f + 0.5f);
    totalMinutes %= 24 * 60;
    const int hh = totalMinutes / 60;
    const int mm = totalMinutes % 60;
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", hh, mm);
    return buffer;
}

} // namespace frontier::scene
