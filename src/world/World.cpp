#include "world/World.h"

#include "core/Log.h"
#include "scene/Camera.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace derecho::world {

bool World::Generate(const core::WorldConfig& config) {
    m_settings = config;

    scene::TerrainSettings settings;
    settings.seed = static_cast<uint32_t>(config.seed);
    settings.size = config.terrainSize;
    settings.resolution = config.terrainResolution;
    settings.amplitude = config.terrainAmplitude;
    settings.waterLevel = config.waterLevel;
    m_terrain.Generate(settings);

    m_water.Configure(config.waterLevel, config.terrainSize);

    m_spawnPoint = m_terrain.SuggestSpawn();
    m_spawnPoint.y += 0.25f;

    scene::VegetationSettings vegetation;
    vegetation.seed = static_cast<uint32_t>(config.seed);
    vegetation.treeCount = config.treeCount;
    vegetation.rockCount = config.rockCount;
    vegetation.grassCount = config.grassCount;
    vegetation.viewDistance = 900.0f;
    vegetation.enabled = true;
    vegetation.avoidPosition = m_spawnPoint;
    vegetation.avoidRadius = 9.0f;
    m_vegetation.Generate(m_terrain, vegetation);

    m_time.Configure(config.startTimeOfDay, config.dayLengthMinutes, false);

    RecomputeLighting();
    m_generated = true;

    std::ostringstream description;
    description << "Seed " << config.seed << " | " << static_cast<int>(config.terrainSize)
                << " m world | " << config.terrainResolution << " terrain quads | "
                << config.treeCount << " trees | " << config.rockCount << " rocks | "
                << config.grassCount << " grass clumps";
    m_description = description.str();

    DE_INFO("World generated: %s", m_description.c_str());
    DE_INFO("Spawn point: (%.1f, %.1f, %.1f)", m_spawnPoint.x, m_spawnPoint.y, m_spawnPoint.z);
    return true;
}

bool World::BuildRenderResources() {
    if (!m_generated) {
        DE_ERROR("World::BuildRenderResources called before Generate().");
        return false;
    }
    if (m_gpuReady) {
        return true;
    }

    if (!m_terrain.BuildRenderResources()) {
        return false;
    }
    if (!m_water.BuildRenderResources(m_terrain)) {
        return false;
    }
    m_vegetation.BuildRenderResources();
    m_gpuReady = true;
    DE_INFO("World render resources ready (%zu terrain triangles, %zu vegetation source triangles).",
            m_terrain.TriangleCount(), m_vegetation.TotalTriangles());
    return true;
}

void World::ReleaseRenderResources() {
    m_vegetation.ReleaseRenderResources();
    m_water.ReleaseRenderResources();
    m_terrain.ReleaseRenderResources();
    m_gpuReady = false;
}

void World::Regenerate(const core::WorldConfig& config) {
    ReleaseRenderResources();
    m_generated = false;
    Generate(config);
    BuildRenderResources();
}

void World::Update(float deltaSeconds, const scene::Camera& camera, const scene::Frustum& frustum,
                   const core::RenderConfig& renderConfig) {
    if (!m_generated) {
        return;
    }
    m_time.Update(deltaSeconds);
    UpdateWind(deltaSeconds);
    RecomputeLighting();

    scene::VegetationSettings vegetation;
    vegetation.seed = static_cast<uint32_t>(m_settings.seed);
    vegetation.treeCount = m_settings.treeCount;
    vegetation.rockCount = m_settings.rockCount;
    vegetation.grassCount = m_settings.grassCount;
    vegetation.viewDistance = renderConfig.viewDistance;
    vegetation.enabled = renderConfig.showFoliage;
    vegetation.avoidPosition = m_spawnPoint;
    m_vegetation.Update(camera, frustum, vegetation);
}

void World::UpdateWind(float deltaSeconds) {
    m_windTime += deltaSeconds;
    // Slowly shifting wind direction with gusting strength.
    const float baseAngle = std::atan2(0.57f, 0.82f);
    const float angle = baseAngle + std::sin(m_windTime * 0.045f) * 0.45f;
    m_windDirection = vec3(std::cos(angle), 0.0f, std::sin(angle));
    const float gust = 0.5f + 0.5f * std::sin(m_windTime * 0.31f) * std::sin(m_windTime * 0.13f + 1.7f);
    m_windStrength = 0.16f + gust * 0.22f;
}

void World::RecomputeLighting() {
    const scene::SunSample sun = m_time.Sun();
    m_lighting.sun = sun;
    m_lighting.daylight = m_time.DaylightFactor();
    m_lighting.stars = m_time.StarFactor();
    m_lighting.moonPosition = m_time.MoonDirection();
    m_lighting.ambientSky = m_time.AmbientSkyColor(sun.color);
    m_lighting.ambientGround = m_time.AmbientGroundColor(sun.color);
    m_lighting.fogColor = m_time.FogColor(sun.color);

    const bool sunUp = sun.elevationDegrees > -2.0f;
    if (sunUp && sun.intensity > 0.001f) {
        m_lighting.lightDirection = sun.direction;
        m_lighting.lightColor = sun.color;
        m_lighting.lightIntensity = sun.intensity;
        m_lighting.shadowStrength = SmoothStep(-2.0f, 4.0f, sun.elevationDegrees);
    } else {
        m_lighting.lightDirection = -m_time.MoonDirection();
        m_lighting.lightColor = m_time.MoonColor();
        m_lighting.lightIntensity = m_time.MoonIntensity();
        m_lighting.shadowStrength = 0.0f;
    }
}

} // namespace derecho::world
