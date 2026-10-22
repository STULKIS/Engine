// Frontier Engine - time of day, sun/moon ephemeris and sky tinting.
#pragma once

#include "math/Math.h"

#include <string>

namespace frontier::scene {

struct SunSample {
    vec3 direction{0.0f, -1.0f, 0.0f}; // direction the light travels
    vec3 position{0.0f, 1.0f, 0.0f};   // unit vector toward the sun
    vec3 color{1.0f};
    float intensity = 1.0f;
    float elevationDegrees = 45.0f;
    float azimuthDegrees = 180.0f;
};

class TimeOfDay {
public:
    TimeOfDay() = default;

    void Configure(float startHours, float dayLengthMinutes, bool paused = false);
    void Update(float deltaSeconds);

    float Hours() const { return m_hours; }
    void SetHours(float hours);
    void SetPaused(bool paused) { m_paused = paused; }
    bool Paused() const { return m_paused; }
    void SetDayLengthMinutes(float minutes) { m_dayLengthMinutes = std::max(1.0f, minutes); }
    float DayLengthMinutes() const { return m_dayLengthMinutes; }

    void SetTimeScale(float scale) { m_timeScale = std::max(0.0f, scale); }
    float TimeScale() const { return m_timeScale; }

    // Sun direction/colour derived from the current hour.
    SunSample Sun() const;

    // Moon light (dim blue) used at night.
    vec3 MoonDirection() const;
    vec3 MoonColor() const;
    float MoonIntensity() const;

    // 0 at civil twilight, 1 in full daylight.
    float DaylightFactor() const;
    float StarFactor() const;

    vec3 AmbientSkyColor(const vec3& sunColor) const;
    vec3 AmbientGroundColor(const vec3& sunColor) const;
    vec3 FogColor(const vec3& sunColor) const;

    std::string PhaseName() const;
    std::string ClockString() const;

private:
    float m_hours = 9.5f;
    float m_dayLengthMinutes = 24.0f;
    float m_timeScale = 1.0f;
    bool m_paused = false;
};

} // namespace frontier::scene
