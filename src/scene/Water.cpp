#include "scene/Water.h"

#include "core/Log.h"
#include "render/GeometryBuilder.h"
#include "scene/Terrain.h"

#include <algorithm>

namespace derecho::scene {

void Water::Configure(float level, float size, int resolution) {
    m_level = level;
    m_size = std::max(16.0f, size);
    m_resolution = std::clamp(resolution, 16, 512);
}

bool Water::BuildRenderResources(const Terrain& terrain) {
    if (terrain.Empty()) {
        DE_ERROR("Water::BuildRenderResources requires generated terrain.");
        return false;
    }
    // The water plane extends slightly beyond the terrain so the horizon never
    // shows a hard edge, and is tessellated for vertex-displaced waves.
    m_size = terrain.Size() * 1.6f;
    m_resolution = 160;

    render::MeshData data = render::geometry::MakeTerrainGrid(
        m_resolution, m_size,
        [this](float, float) { return m_level; },
        [](float, float) { return vec3(0.05f, 0.16f, 0.2f); });

    if (!m_mesh.Create(data)) {
        DE_ERROR("Failed to create water mesh.");
        return false;
    }
    DE_INFO("Water surface built: level %.1f m, %.0f m across, %zu triangles.",
            m_level, m_size, m_mesh.TriangleCount());
    return true;
}

void Water::ReleaseRenderResources() {
    m_mesh.Destroy();
}

} // namespace derecho::scene
