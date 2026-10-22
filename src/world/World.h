// Frontier Engine - assembled procedural world (terrain, water, vegetation, sky).
#pragma once

#include "core/Config.h"
#include "math/Math.h"
#include "scene/Terrain.h"
#include "scene/TimeOfDay.h"
#include "scene/Vegetation.h"
#include "scene/Water.h"

#include <string>

namespace frontier::scene { class Camera; }

namespace frontier::world {

// Combined directional light + atmosphere state produced by the time of day.
struct LightingState {
    scene::SunSample sun;
    vec3 moonPosition{0.0f, 1.0f, 0.0f};
    vec3 lightDirection{0.0f, -1.0f, 0.0f}; // direction of the dominant light
    vec3 lightColor{1.0f};
    float lightIntensity = 1.0f;
    vec3 ambientSky{0.2f};
    vec3 ambientGround{0.1f};
    vec3 fogColor{0.6f, 0.7f, 0.8f};
    float daylight = 1.0f;
    float stars = 0.0f;
    float shadowStrength = 1.0f;
};

class World {
public:
    World() = default;

    // CPU-only world generation.
    bool Generate(const core::WorldConfig& config);
    bool BuildRenderResources();
    void ReleaseRenderResources();

    void Update(float deltaSeconds, const scene::Camera& camera, const scene::Frustum& frustum,
                const core::RenderConfig& renderConfig);

    scene::Terrain& Terrain() { return m_terrain; }
    const scene::Terrain& Terrain() const { return m_terrain; }
    scene::Water& Water() { return m_water; }
    const scene::Water& Water() const { return m_water; }
    scene::Vegetation& Vegetation() { return m_vegetation; }
    const scene::Vegetation& Vegetation() const { return m_vegetation; }
    scene::TimeOfDay& Time() { return m_time; }
    const scene::TimeOfDay& Time() const { return m_time; }

    const LightingState& Lighting() const { return m_lighting; }
    const vec3& SpawnPoint() const { return m_spawnPoint; }
    const core::WorldConfig& Settings() const { return m_settings; }

    // Wind used by the vegetation shaders (direction + strength).
    const vec3& WindDirection() const { return m_windDirection; }
    float WindStrength() const { return m_windStrength; }

    const std::string& Description() const { return m_description; }

    void Regenerate(const core::WorldConfig& config);

private:
    void RecomputeLighting();
    void UpdateWind(float deltaSeconds);

    core::WorldConfig m_settings;
    scene::Terrain m_terrain;
    scene::Water m_water;
    scene::Vegetation m_vegetation;
    scene::TimeOfDay m_time;
    LightingState m_lighting;
    vec3 m_spawnPoint = vec3(0.0f);
    vec3 m_windDirection = vec3(0.82f, 0.0f, 0.57f);
    float m_windStrength = 0.22f;
    float m_windTime = 0.0f;
    bool m_generated = false;
    bool m_gpuReady = false;
    std::string m_description;
};

} // namespace frontier::world
