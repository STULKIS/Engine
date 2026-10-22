// Frontier Engine - procedural heightfield terrain.
//
// The CPU heightfield is generated independently from GPU resources so that
// headless simulation, collision and foliage placement all work without a GL
// context. BuildRenderResources() performs the GL upload once a context exists.
#pragma once

#include "math/Math.h"
#include "render/Mesh.h"
#include "render/Texture.h"
#include "scene/Frustum.h"

#include <memory>
#include <vector>

namespace frontier::core { class FractalNoise; }
namespace frontier::render { class Shader; }

namespace frontier::scene {

struct TerrainSettings {
    uint32_t seed = 1337;
    float size = 900.0f;
    int resolution = 256;
    float amplitude = 58.0f;
    float waterLevel = 0.0f;
};

class Terrain {
public:
    Terrain();
    ~Terrain();

    Terrain(const Terrain&) = delete;
    Terrain& operator=(const Terrain&) = delete;

    void Generate(const TerrainSettings& settings);
    bool Empty() const { return m_heights.empty(); }

    float HeightAt(float worldX, float worldZ) const;
    vec3 NormalAt(float worldX, float worldZ) const;
    float SlopeAt(float worldX, float worldZ) const; // 0 = flat, 1 = vertical
    float WaterDepthAt(float worldX, float worldZ) const;
    bool IsUnderwater(float worldX, float worldZ) const;

    // Picks a scenic, walkable spawn near the lake shore.
    vec3 SuggestSpawn() const;

    // Builds GPU meshes and the height texture. Requires a current GL context.
    bool BuildRenderResources();
    void ReleaseRenderResources();

    const TerrainSettings& Settings() const { return m_settings; }
    float Size() const { return m_settings.size; }
    float WaterLevel() const { return m_settings.waterLevel; }
    float MinHeight() const { return m_minHeight; }
    float MaxHeight() const { return m_maxHeight; }
    int Resolution() const { return m_settings.resolution; }
    const std::vector<float>& Heights() const { return m_heights; }
    const scene::AABB& Bounds() const { return m_bounds; }

    render::Mesh& Mesh() { return m_mesh; }
    const render::Mesh& Mesh() const { return m_mesh; }
    render::Texture& HeightTexture() { return m_heightTexture; }
    const render::Texture& HeightTexture() const { return m_heightTexture; }
    size_t TriangleCount() const { return m_mesh.TriangleCount(); }

private:
    float SampleGrid(int ix, int iz) const;
    vec3 MaterialColor(float worldX, float worldZ, float height, const vec3& normal) const;
    float EvaluateHeight(float worldX, float worldZ) const;

    TerrainSettings m_settings;
    std::vector<float> m_heights;
    int m_samples = 0;
    float m_minHeight = 0.0f;
    float m_maxHeight = 0.0f;
    scene::AABB m_bounds;
    render::Mesh m_mesh;
    render::Texture m_heightTexture;

    // Fractal noise fields are kept alive between generations for cheap queries.
    std::unique_ptr<core::FractalNoise> m_rolling;
    std::unique_ptr<core::FractalNoise> m_ridges;
    std::unique_ptr<core::FractalNoise> m_masks;
    std::unique_ptr<core::FractalNoise> m_detail;
};

} // namespace frontier::scene
