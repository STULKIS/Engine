#include "scene/Vegetation.h"

#include "core/Log.h"
#include "core/Noise.h"
#include "render/GeometryBuilder.h"
#include "scene/Terrain.h"

#include <algorithm>
#include <cmath>

namespace frontier::scene {
namespace {

mat4 ComposeTransform(const vec3& position, float rotation, float scale) {
    mat4 m = glm::translate(mat4(1.0f), position);
    m = glm::rotate(m, rotation, vec3(0, 1, 0));
    m = glm::scale(m, vec3(scale));
    return m;
}

float MeshRadius(const render::Mesh& mesh, float scale) {
    const scene::AABB& b = mesh.Bounds();
    const vec3 extents = b.Extents();
    return glm::length(extents) * scale;
}

} // namespace

void FoliageSet::SortIntoVisible(const Camera& camera, const Frustum& frustum, float maxDistance) {
    visibleTotal = 0;
    culledTotal = 0;
    if (lodDistances.empty()) {
        return;
    }

    // Without GL resources (headless mode) we still evaluate distance and
    // frustum visibility so the statistics remain meaningful.
    if (visible.empty()) {
        const vec3 cameraPos = camera.Position();
        const float maxDistanceForSet = std::min(maxDistance, lodDistances.back());
        for (const FoliageInstance& instance : instances) {
            const bool visibleNow = glm::length(instance.position - cameraPos) <= maxDistanceForSet &&
                                    frustum.ContainsSphere(instance.position, instance.radius);
            if (visibleNow) {
                ++visibleTotal;
            } else {
                ++culledTotal;
            }
        }
        return;
    }
    for (std::vector<render::InstanceData>& list : visible) {
        list.clear();
    }

    const vec3 cameraPos = camera.Position();
    for (const FoliageInstance& instance : instances) {
        const float distance = glm::length(instance.position - cameraPos);
        int lod = LodCount() - 1;
        for (int i = 0; i < LodCount() && i < static_cast<int>(lodDistances.size()); ++i) {
            if (distance <= lodDistances[static_cast<size_t>(i)]) {
                lod = i;
                break;
            }
        }
        lod = std::clamp(lod, 0, static_cast<int>(visible.size()) - 1);
        const float maxDistanceForSet = std::min(maxDistance, lodDistances.back());
        if (distance > maxDistanceForSet) {
            ++culledTotal;
            continue;
        }
        if (!frustum.ContainsSphere(instance.position, instance.radius)) {
            ++culledTotal;
            continue;
        }
        render::InstanceData data;
        data.transform = ComposeTransform(instance.position, instance.rotation, instance.scale);
        data.tint = instance.tint;
        visible[static_cast<size_t>(lod)].push_back(data);
        ++visibleTotal;
    }

    for (size_t i = 0; i < visible.size(); ++i) {
        visibleCounts[i] = static_cast<int>(visible[i].size());
    }
}

void FoliageSet::UploadVisible() {
    for (size_t lod = 0; lod < meshes.size(); ++lod) {
        if (!visible[lod].empty() && visibleCounts[lod] > 0) {
            meshes[lod].UploadInstances(visible[lod]);
        }
    }
}

size_t FoliageSet::TriangleCount() const {
    size_t total = 0;
    for (const render::Mesh& mesh : meshes) {
        total += mesh.TriangleCount();
    }
    return total;
}

void Vegetation::Generate(const Terrain& terrain, const VegetationSettings& settings) {
    m_settings = settings;
    m_sets.clear();
    m_forceUpdate = true;

    if (!settings.enabled) {
        FR_INFO("Vegetation generation disabled by configuration.");
        return;
    }

    GenerateTrees(terrain, settings);
    GenerateRocks(terrain, settings);
    GenerateGroundCover(terrain, settings);

    m_stats = {};
    RefreshStats();
    FR_INFO("Vegetation placed: %d conifers, %d broadleaf, %d rocks, %d grass, %d shrubs.",
            static_cast<int>(m_sets.size() > 0 ? m_sets[0].instances.size() : 0),
            static_cast<int>(m_sets.size() > 1 ? m_sets[1].instances.size() : 0),
            static_cast<int>(m_sets.size() > 2 ? m_sets[2].instances.size() : 0),
            static_cast<int>(m_sets.size() > 3 ? m_sets[3].instances.size() : 0),
            static_cast<int>(m_sets.size() > 4 ? m_sets[4].instances.size() : 0));
}

void Vegetation::GenerateTrees(const Terrain& terrain, const VegetationSettings& settings) {
    core::Random rng(settings.seed * 7477u + 23u);
    core::FractalNoise forestNoise(settings.seed + 401u, 4, 2.1f, 0.52f);
    const float half = terrain.Size() * 0.5f - 24.0f;
    const float water = terrain.WaterLevel();

    FoliageSet conifers;
    conifers.name = "Conifers";
    conifers.lodDistances = {150.0f, 380.0f, 900.0f};
    conifers.castsShadow = true;
    conifers.wind = true;

    FoliageSet broadleaf;
    broadleaf.name = "Broadleaf trees";
    broadleaf.lodDistances = {130.0f, 330.0f, 900.0f};
    broadleaf.castsShadow = true;
    broadleaf.wind = true;

    const int totalTrees = std::max(0, settings.treeCount);
    for (int i = 0; i < totalTrees; ++i) {
        bool placed = false;
        for (int attempt = 0; attempt < 32 && !placed; ++attempt) {
            const float x = rng.Range(-half, half);
            const float z = rng.Range(-half, half);
            const float height = terrain.HeightAt(x, z);
            if (height < water + 2.0f) {
                continue;
            }
            if (settings.avoidRadius > 0.0f &&
                glm::length(vec3(x, height, z) - settings.avoidPosition) < settings.avoidRadius) {
                continue;
            }
            const vec3 normal = terrain.NormalAt(x, z);
            if (normal.y < 0.78f) {
                continue;
            }
            // Forest patches plus scatter.
            const float density = forestNoise.FBM(x * 0.006f, z * 0.006f) * 0.5f + 0.5f;
            if (!rng.Chance(0.15f + density * 0.85f)) {
                continue;
            }

            FoliageInstance instance;
            instance.position = vec3(x, height - 0.25f, z);
            instance.rotation = rng.Range(0.0f, kTwoPi);
            instance.scale = rng.Range(0.72f, 1.35f);
            const float greenShift = rng.Range(0.85f, 1.12f);
            instance.tint = vec4(greenShift, rng.Range(0.9f, 1.08f), greenShift * rng.Range(0.85f, 1.0f),
                                 rng.Range(0.0f, kTwoPi));
            // Radius is refined once meshes exist; a conservative value keeps
            // the wide billboard LOD visible at distance.
            instance.radius = 7.5f * instance.scale;
            placed = true;

            if (rng.Chance(0.62f)) {
                conifers.instances.push_back(instance);
            } else {
                broadleaf.instances.push_back(instance);
            }
        }
    }

    m_sets.push_back(std::move(conifers));
    m_sets.push_back(std::move(broadleaf));
}

void Vegetation::GenerateRocks(const Terrain& terrain, const VegetationSettings& settings) {
    core::Random rng(settings.seed * 31337u + 7u);
    const float half = terrain.Size() * 0.5f - 12.0f;
    const float water = terrain.WaterLevel();

    FoliageSet rocks;
    rocks.name = "Rocks";
    rocks.lodDistances = {90.0f, 240.0f, 620.0f};
    rocks.castsShadow = true;
    rocks.wind = false;

    const int count = std::max(0, settings.rockCount);
    for (int i = 0; i < count; ++i) {
        bool placed = false;
        for (int attempt = 0; attempt < 24 && !placed; ++attempt) {
            const float x = rng.Range(-half, half);
            const float z = rng.Range(-half, half);
            const float height = terrain.HeightAt(x, z);
            if (height < water - 1.5f) {
                continue;
            }
            if (settings.avoidRadius > 0.0f &&
                glm::length(vec3(x, height, z) - settings.avoidPosition) < settings.avoidRadius * 0.66f) {
                continue;
            }
            const vec3 normal = terrain.NormalAt(x, z);
            // Rocks favour slopes and shorelines.
            const float slope = 1.0f - normal.y;
            const float shoreline = 1.0f - SmoothStep(0.0f, 3.0f, height - water);
            if (!rng.Chance(0.35f + slope * 1.5f + shoreline * 0.35f)) {
                continue;
            }

            FoliageInstance instance;
            const float scale = rng.Range(0.45f, 2.6f);
            instance.position = vec3(x, height - scale * 0.35f, z);
            instance.rotation = rng.Range(0.0f, kTwoPi);
            instance.scale = scale;
            const float tone = rng.Range(0.8f, 1.15f);
            instance.tint = vec4(tone, tone * rng.Range(0.96f, 1.02f), tone * rng.Range(0.9f, 0.99f), 0.0f);
            instance.radius = 1.4f * scale;
            rocks.instances.push_back(instance);
            placed = true;
        }
    }
    m_sets.push_back(std::move(rocks));
}

void Vegetation::GenerateGroundCover(const Terrain& terrain, const VegetationSettings& settings) {
    core::Random rng(settings.seed * 65537u + 91u);
    core::FractalNoise clumpNoise(settings.seed + 887u, 4, 2.0f, 0.55f);
    const float half = terrain.Size() * 0.5f - 6.0f;
    const float water = terrain.WaterLevel();

    FoliageSet grass;
    grass.name = "Grass";
    grass.lodDistances = {32.0f, 78.0f, 135.0f};
    grass.castsShadow = false;
    grass.wind = true;

    const int grassCount = std::max(0, settings.grassCount);
    for (int i = 0; i < grassCount; ++i) {
        bool placed = false;
        for (int attempt = 0; attempt < 6 && !placed; ++attempt) {
            const float x = rng.Range(-half, half);
            const float z = rng.Range(-half, half);
            const float height = terrain.HeightAt(x, z);
            if (height < water + 0.5f) {
                continue;
            }
            const vec3 normal = terrain.NormalAt(x, z);
            if (normal.y < 0.87f) {
                continue;
            }
            const float clump = clumpNoise.FBM(x * 0.02f, z * 0.02f) * 0.5f + 0.5f;
            if (!rng.Chance(0.25f + clump * 0.9f)) {
                continue;
            }

            FoliageInstance instance;
            instance.position = vec3(x, height - 0.05f, z);
            instance.rotation = rng.Range(0.0f, kTwoPi);
            instance.scale = rng.Range(0.7f, 1.5f);
            const float lush = 0.55f + clump * 0.75f;
            instance.tint = vec4(lush * rng.Range(0.85f, 1.1f), lush * rng.Range(0.9f, 1.15f),
                                 lush * rng.Range(0.7f, 0.95f), rng.Range(0.0f, kTwoPi));
            instance.radius = 1.6f * instance.scale;
            grass.instances.push_back(instance);
            placed = true;
        }
    }
    m_sets.push_back(std::move(grass));

    // Shrubs are cheaper than trees and break up the mid-ground.
    FoliageSet shrubs;
    shrubs.name = "Shrubs";
    shrubs.lodDistances = {55.0f, 120.0f, 220.0f};
    shrubs.castsShadow = true;
    shrubs.wind = true;
    const int shrubCount = std::max(0, grassCount / 26);
    for (int i = 0; i < shrubCount; ++i) {
        for (int attempt = 0; attempt < 12; ++attempt) {
            const float x = rng.Range(-half, half);
            const float z = rng.Range(-half, half);
            const float height = terrain.HeightAt(x, z);
            if (height < water + 1.0f || terrain.NormalAt(x, z).y < 0.8f) {
                continue;
            }
            if (settings.avoidRadius > 0.0f &&
                glm::length(vec3(x, height, z) - settings.avoidPosition) < settings.avoidRadius * 0.5f) {
                continue;
            }
            FoliageInstance instance;
            instance.position = vec3(x, height - 0.1f, z);
            instance.rotation = rng.Range(0.0f, kTwoPi);
            instance.scale = rng.Range(0.6f, 1.5f);
            const float tone = rng.Range(0.8f, 1.2f);
            instance.tint = vec4(tone, tone * rng.Range(0.95f, 1.1f), tone * 0.85f, rng.Range(0.0f, kTwoPi));
            instance.radius = 1.1f * instance.scale;
            shrubs.instances.push_back(instance);
            break;
        }
    }
    m_sets.push_back(std::move(shrubs));
}

void Vegetation::BuildRenderResources() {
    if (m_sets.empty()) {
        return;
    }

    uint32_t meshSeed = m_settings.seed * 977u + 3u;
    for (FoliageSet& set : m_sets) {
        set.meshes.clear();
        set.visible.clear();

        if (set.name == "Conifers") {
            for (int lod = 0; lod < 3; ++lod) {
                render::Mesh mesh;
                mesh.Create(render::geometry::MakeConiferTree(meshSeed + static_cast<uint32_t>(lod) * 13u, lod));
                set.meshes.push_back(std::move(mesh));
            }
        } else if (set.name == "Broadleaf trees") {
            for (int lod = 0; lod < 3; ++lod) {
                render::Mesh mesh;
                mesh.Create(render::geometry::MakeBroadleafTree(meshSeed + 100u + static_cast<uint32_t>(lod) * 17u, lod));
                set.meshes.push_back(std::move(mesh));
            }
        } else if (set.name == "Rocks") {
            for (int lod = 0; lod < 3; ++lod) {
                render::Mesh mesh;
                mesh.Create(render::geometry::MakeRock(meshSeed + 200u + static_cast<uint32_t>(lod),
                                                       lod == 0 ? 2 : (lod == 1 ? 1 : 0), 0.55f));
                set.meshes.push_back(std::move(mesh));
            }
        } else if (set.name == "Grass") {
            render::Mesh nearMesh;
            nearMesh.Create(render::geometry::MakeGrassClump(meshSeed + 300u, 8, 0.42f, 0.85f));
            set.meshes.push_back(std::move(nearMesh));

            render::Mesh midMesh;
            midMesh.Create(render::geometry::MakeGrassClump(meshSeed + 301u, 3, 0.5f, 0.75f));
            set.meshes.push_back(std::move(midMesh));

            render::Mesh farMesh;
            farMesh.Create(render::geometry::MakeCrossCard(1.3f, 0.8f, vec4(0.12f, 0.22f, 0.08f, 0.0f),
                                                           vec4(0.3f, 0.45f, 0.16f, 0.4f)));
            set.meshes.push_back(std::move(farMesh));
        } else {
            render::Mesh shrub;
            shrub.Create(render::geometry::MakeShrub(meshSeed + 400u));
            set.meshes.push_back(std::move(shrub));

            render::Mesh mid;
            mid.Create(render::geometry::MakeRock(meshSeed + 401u, 0, 0.6f));
            set.meshes.push_back(std::move(mid));

            // Note: "far" is a legacy Microsoft compiler keyword, so the
            // variable is called farMesh like in the branch above.
            render::Mesh farMesh;
            farMesh.Create(render::geometry::MakeCrossCard(1.6f, 1.1f, vec4(0.1f, 0.18f, 0.07f, 0.0f),
                                                          vec4(0.22f, 0.34f, 0.12f, 0.4f)));
            set.meshes.push_back(std::move(farMesh));
        }

        set.visible.resize(set.meshes.size());
        set.visibleCounts.assign(set.meshes.size(), 0);

        // Refine culling radii using the real LOD0 geometry.
        if (!set.meshes.empty() && !set.instances.empty()) {
            const float baseRadius = MeshRadius(set.meshes[0], 1.0f);
            for (FoliageInstance& instance : set.instances) {
                instance.radius = baseRadius * instance.scale;
            }
            // Shadow/actual render needs instance buffers sized up front.
            for (render::Mesh& mesh : set.meshes) {
                mesh.EnsureInstanceCapacity(256);
            }
        }
    }

    m_forceUpdate = true;
    m_needsUpload = true;
    FR_INFO("Vegetation render resources built.");
}

void Vegetation::Update(const Camera& camera, const Frustum& frustum, const VegetationSettings& settings) {
    if (m_sets.empty()) {
        return;
    }
    const float maxDistance = settings.viewDistance;
    const bool moved = glm::distance(camera.Position(), m_lastCameraPosition) > 0.75f ||
                       glm::dot(camera.Forward(), m_lastCameraForward) < 0.9995f ||
                       std::fabs(maxDistance - m_lastViewDistance) > 0.5f ||
                       m_forceUpdate;

    if (moved) {
        for (FoliageSet& set : m_sets) {
            set.SortIntoVisible(camera, frustum, maxDistance);
        }
        MarkCameraMoved(camera, maxDistance);
        m_forceUpdate = false;
        m_needsUpload = true;
    }
    UpdateStats();
}

void Vegetation::MarkCameraMoved(const Camera& camera, float maxDistance) {
    m_lastCameraPosition = camera.Position();
    m_lastCameraForward = camera.Forward();
    m_lastViewDistance = maxDistance;
}

void Vegetation::UploadVisible() {
    if (!m_needsUpload) {
        return;
    }
    for (FoliageSet& set : m_sets) {
        set.UploadVisible();
    }
    m_needsUpload = false;
}

void Vegetation::RefreshStats() {
    m_stats.totalInstances = 0;
    for (const FoliageSet& set : m_sets) {
        m_stats.totalInstances += static_cast<int>(set.instances.size());
    }
}

void Vegetation::UpdateStats() {
    RefreshStats();
    m_stats.visibleInstances = 0;
    m_stats.culledInstances = 0;
    m_stats.drawCalls = 0;
    m_stats.trianglesDrawn = 0;
    for (const FoliageSet& set : m_sets) {
        m_stats.visibleInstances += set.visibleTotal;
        m_stats.culledInstances += set.culledTotal;
        for (size_t lod = 0; lod < set.meshes.size(); ++lod) {
            if (lod < set.visibleCounts.size() && set.visibleCounts[lod] > 0) {
                ++m_stats.drawCalls;
                m_stats.trianglesDrawn += set.meshes[lod].TriangleCount() *
                                          static_cast<size_t>(set.visibleCounts[lod]);
            }
        }
    }
}

size_t Vegetation::TotalTriangles() const {
    size_t total = 0;
    for (const FoliageSet& set : m_sets) {
        for (const render::Mesh& mesh : set.meshes) {
            total += mesh.TriangleCount();
        }
    }
    return total;
}

void Vegetation::ReleaseRenderResources() {
    for (FoliageSet& set : m_sets) {
        for (render::Mesh& mesh : set.meshes) {
            mesh.Destroy();
        }
        set.meshes.clear();
        set.visible.clear();
        set.visibleCounts.clear();
    }
}

} // namespace frontier::scene
