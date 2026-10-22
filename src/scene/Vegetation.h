// Frontier Engine - instanced procedural vegetation with wind, frustum culling
// and distance LOD.
#pragma once

#include "math/Math.h"
#include "render/Mesh.h"
#include "scene/Camera.h"
#include "scene/Frustum.h"

#include <string>
#include <vector>

namespace frontier::scene {

class Terrain;

struct FoliageInstance {
    vec3 position{0.0f};
    float rotation = 0.0f;
    float scale = 1.0f;
    vec4 tint{1.0f};   // rgb = colour multiplier, a = wind phase
    float radius = 1.0f;
};

// One drawable vegetation family: several LOD meshes plus the instances that
// survive this frame's distance and frustum tests.
class FoliageSet {
public:
    std::string name;
    std::vector<render::Mesh> meshes;
    std::vector<float> lodDistances;  // distance threshold per LOD
    std::vector<FoliageInstance> instances;
    std::vector<std::vector<render::InstanceData>> visible;
    std::vector<int> visibleCounts;
    bool castsShadow = true;
    bool wind = true;
    int visibleTotal = 0;
    int culledTotal = 0;

    int LodCount() const { return static_cast<int>(meshes.size()); }

    void SortIntoVisible(const Camera& camera, const Frustum& frustum, float maxDistance);
    void UploadVisible();

    size_t TriangleCount() const;
};

struct VegetationSettings {
    uint32_t seed = 1337;
    int treeCount = 900;
    int rockCount = 320;
    int grassCount = 60000;
    float viewDistance = 900.0f;
    bool enabled = true;
    // Keeps large props from spawning on top of the player start position.
    vec3 avoidPosition = vec3(1e6f);
    float avoidRadius = 0.0f;
};

struct VegetationStats {
    int totalInstances = 0;
    int visibleInstances = 0;
    int culledInstances = 0;
    int drawCalls = 0;
    size_t trianglesDrawn = 0;
};

class Vegetation {
public:
    Vegetation() = default;

    // CPU-only placement. Safe to call without an OpenGL context.
    void Generate(const Terrain& terrain, const VegetationSettings& settings);
    void BuildRenderResources();

    // Rebuilds per-LOD visible lists. Skips work when the camera is static.
    void Update(const Camera& camera, const Frustum& frustum, const VegetationSettings& settings);
    void UploadVisible();

    const std::vector<FoliageSet>& Sets() const { return m_sets; }
    std::vector<FoliageSet>& Sets() { return m_sets; }

    const VegetationStats& Stats() const { return m_stats; }
    void RefreshStats();
    size_t TotalTriangles() const;

    void ReleaseRenderResources();

private:
    void GenerateTrees(const Terrain& terrain, const VegetationSettings& settings);
    void GenerateRocks(const Terrain& terrain, const VegetationSettings& settings);
    void GenerateGroundCover(const Terrain& terrain, const VegetationSettings& settings);
    void MarkCameraMoved(const Camera& camera, float maxDistance);
    void UpdateStats();

    std::vector<FoliageSet> m_sets;
    VegetationSettings m_settings;
    VegetationStats m_stats;

    vec3 m_lastCameraPosition = vec3(1e6f);
    vec3 m_lastCameraForward = vec3(0.0f);
    float m_lastViewDistance = 0.0f;
    bool m_forceUpdate = true;
    bool m_needsUpload = false;
};

} // namespace frontier::scene
