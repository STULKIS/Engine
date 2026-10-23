#include "scene/Terrain.h"

#include "core/Log.h"
#include "core/Noise.h"
#include "render/GeometryBuilder.h"

#include <algorithm>
#include <cmath>

namespace derecho::scene {
// Defined here so the unique_ptr members see complete noise types.
Terrain::Terrain() = default;
Terrain::~Terrain() = default;

void Terrain::Generate(const TerrainSettings& settings) {
    m_settings = settings;
    m_settings.resolution = std::clamp(m_settings.resolution, 16, 1024);
    m_samples = m_settings.resolution + 1;

    m_rolling = std::make_unique<core::FractalNoise>(settings.seed + 101u, 5, 2.05f, 0.52f);
    m_ridges = std::make_unique<core::FractalNoise>(settings.seed + 257u, 4, 2.2f, 0.48f);
    m_masks = std::make_unique<core::FractalNoise>(settings.seed + 911u, 3, 2.0f, 0.55f);
    m_detail = std::make_unique<core::FractalNoise>(settings.seed + 1297u, 3, 2.4f, 0.5f);

    m_heights.assign(static_cast<size_t>(m_samples) * m_samples, 0.0f);
    m_minHeight = 1e30f;
    m_maxHeight = -1e30f;

    const float half = m_settings.size * 0.5f;
    const float step = m_settings.size / static_cast<float>(m_settings.resolution);

    for (int z = 0; z < m_samples; ++z) {
        for (int x = 0; x < m_samples; ++x) {
            const float wx = -half + static_cast<float>(x) * step;
            const float wz = -half + static_cast<float>(z) * step;
            const float h = EvaluateHeight(wx, wz);
            m_heights[static_cast<size_t>(z) * m_samples + x] = h;
            m_minHeight = std::min(m_minHeight, h);
            m_maxHeight = std::max(m_maxHeight, h);
        }
    }

    m_bounds = {vec3(-half, m_minHeight, -half), vec3(half, m_maxHeight, half)};
    DE_INFO("Terrain generated: %.0f m, %d samples, height range %.1f..%.1f m, seed %u",
            m_settings.size, m_samples, m_minHeight, m_maxHeight, m_settings.seed);
}

float Terrain::EvaluateHeight(float x, float z) const {
    if (!m_rolling) {
        return 0.0f;
    }
    const float amplitude = m_settings.amplitude;

    // Large rolling hills.
    const float rolling = m_rolling->FBM(x * 0.0019f, z * 0.0019f); // ~[-1,1]

    // Mountain ridges are masked by a very low frequency field so they form
    // distinct ranges instead of covering the whole map.
    const float ridge = m_ridges->Ridged(x * 0.0012f + 3.7f, z * 0.0012f - 8.1f);
    float mask = m_masks->FBM(x * 0.00065f - 11.0f, z * 0.00065f + 4.0f) * 0.5f + 0.5f;
    mask = SmoothStep(0.48f, 0.82f, mask);
    const float mountains = std::pow(ridge, 1.7f) * mask;

    // Fine surface detail (also gives the terrain silhouette some character).
    const float detail = m_detail->FBM(x * 0.011f, z * 0.011f);

    float height = 3.0f + amplitude * (0.30f + rolling * 0.42f + mountains * 0.95f + detail * 0.035f);

    // Lake basin: a smooth bowl around the origin keeps a large body of water
    // in the playable centre of the map.
    const float lakeDistance = std::sqrt(x * x + z * z);
    const float basin = SmoothStep(m_settings.size * 0.30f, m_settings.size * 0.015f, lakeDistance);
    height -= basin * amplitude * 0.62f;

    // Outer rim hills frame the map and hide the world edge.
    const float half = m_settings.size * 0.5f;
    const float edge = std::max(std::fabs(x), std::fabs(z)) / std::max(half, 1.0f);
    const float rim = SmoothStep(0.72f, 1.0f, edge);
    height += rim * amplitude * 0.55f;
    height += std::pow(rim, 2.2f) * amplitude * 0.5f;

    // Shallow river valley cutting from the lake toward the south-east.
    const float riverAxis = x * 0.55f - z * 0.83f;
    const float riverWidth = 26.0f + 16.0f * (m_detail->FBM(z * 0.01f, x * 0.01f) * 0.5f + 0.5f);
    const float riverMask = SmoothStep(m_settings.size * 0.42f, m_settings.size * 0.1f,
                                       std::sqrt(x * x + z * z));
    const float river = SmoothStep(riverWidth, 0.0f, std::fabs(riverAxis)) * riverMask;
    height -= river * amplitude * 0.12f;

    return height;
}

float Terrain::SampleGrid(int ix, int iz) const {
    ix = std::clamp(ix, 0, m_samples - 1);
    iz = std::clamp(iz, 0, m_samples - 1);
    return m_heights[static_cast<size_t>(iz) * m_samples + ix];
}

float Terrain::HeightAt(float worldX, float worldZ) const {
    if (m_heights.empty()) {
        return 0.0f;
    }
    const float half = m_settings.size * 0.5f;
    const float normalizedX = (worldX + half) / m_settings.size * static_cast<float>(m_settings.resolution);
    const float normalizedZ = (worldZ + half) / m_settings.size * static_cast<float>(m_settings.resolution);

    const int x0 = static_cast<int>(std::floor(normalizedX));
    const int z0 = static_cast<int>(std::floor(normalizedZ));
    const float fx = Clamp(normalizedX - static_cast<float>(x0), 0.0f, 1.0f);
    const float fz = Clamp(normalizedZ - static_cast<float>(z0), 0.0f, 1.0f);

    const float h00 = SampleGrid(x0, z0);
    const float h10 = SampleGrid(x0 + 1, z0);
    const float h01 = SampleGrid(x0, z0 + 1);
    const float h11 = SampleGrid(x0 + 1, z0 + 1);
    return Lerp(Lerp(h00, h10, fx), Lerp(h01, h11, fx), fz);
}

vec3 Terrain::NormalAt(float worldX, float worldZ) const {
    const float step = std::max(m_settings.size / static_cast<float>(std::max(1, m_settings.resolution)), 1.0f);
    const float hL = HeightAt(worldX - step, worldZ);
    const float hR = HeightAt(worldX + step, worldZ);
    const float hD = HeightAt(worldX, worldZ - step);
    const float hU = HeightAt(worldX, worldZ + step);
    return glm::normalize(vec3(hL - hR, 2.0f * step, hD - hU));
}

float Terrain::SlopeAt(float worldX, float worldZ) const {
    const vec3 n = NormalAt(worldX, worldZ);
    return Saturate(1.0f - n.y);
}

float Terrain::WaterDepthAt(float worldX, float worldZ) const {
    return m_settings.waterLevel - HeightAt(worldX, worldZ);
}

bool Terrain::IsUnderwater(float worldX, float worldZ) const {
    return WaterDepthAt(worldX, worldZ) > 0.0f;
}

vec3 Terrain::SuggestSpawn() const {
    const float half = m_settings.size * 0.5f;
    const float water = m_settings.waterLevel;
    const float step = m_settings.size / static_cast<float>(m_settings.resolution);
    const int stride = std::max(1, m_settings.resolution / 128);

    vec3 best(0.0f, 0.0f, 0.0f);
    float bestScore = -1e9f;
    bool found = false;

    // Scan the heightfield for a flat, dry spot with a clear view of the lake.
    for (int iz = stride; iz < m_samples - stride; iz += stride) {
        for (int ix = stride; ix < m_samples - stride; ix += stride) {
            const float x = -half + static_cast<float>(ix) * step;
            const float z = -half + static_cast<float>(iz) * step;
            const float radius = std::sqrt(x * x + z * z);
            if (radius > half - 30.0f) {
                continue;
            }
            const float h = m_heights[static_cast<size_t>(iz) * m_samples + ix];
            if (h < water + 1.8f || h > water + 22.0f) {
                continue;
            }
            const vec3 normal = NormalAt(x, z);
            if (normal.y < 0.87f) {
                continue;
            }
            // Flatness matters most; stay reasonably close to the shoreline.
            const float score = normal.y * 5.0f - std::fabs(h - water - 3.2f) * 0.5f -
                                std::max(0.0f, radius - 150.0f) * 0.03f;
            if (score > bestScore) {
                bestScore = score;
                best = vec3(x, h, z);
                found = true;
            }
        }
    }

    if (!found) {
        // Last resort: the highest interior point, so the player never spawns
        // underwater or outside the map.
        best = vec3(0.0f, HeightAt(0.0f, 0.0f), 0.0f);
        for (int iz = 0; iz < m_samples; ++iz) {
            for (int ix = 0; ix < m_samples; ++ix) {
                const float h = m_heights[static_cast<size_t>(iz) * m_samples + ix];
                if (h > best.y) {
                    best = vec3(-half + static_cast<float>(ix) * step, h, -half + static_cast<float>(iz) * step);
                }
            }
        }
        // Clamp into the interior.
        best.x = Clamp(best.x, -half + 20.0f, half - 20.0f);
        best.z = Clamp(best.z, -half + 20.0f, half - 20.0f);
    }
    return best;
}

vec3 Terrain::MaterialColor(float worldX, float worldZ, float height, const vec3& normal) const {
    const float water = m_settings.waterLevel;
    const float slope = 1.0f - normal.y;
    core::Random rng(static_cast<uint32_t>(static_cast<int32_t>(worldX * 7.0f) * 73856093) ^
                     static_cast<uint32_t>(static_cast<int32_t>(worldZ * 7.0f) * 19349663) ^
                     m_settings.seed);
    const float variation = 0.88f + rng.NextFloat() * 0.24f;

    const vec3 grass(0.24f, 0.34f, 0.13f);
    const vec3 dryGrass(0.38f, 0.37f, 0.18f);
    const vec3 rock(0.36f, 0.35f, 0.33f);
    const vec3 sand(0.52f, 0.46f, 0.34f);
    const vec3 snow(0.82f, 0.85f, 0.9f);

    vec3 color = Lerp(grass, dryGrass, Saturate((height - water) * 0.02f));
    const float rockBlend = SmoothStep(0.16f, 0.42f, slope);
    color = Lerp(color, rock, rockBlend);

    const float shore = 1.0f - SmoothStep(0.4f, 2.6f, height - water);
    color = Lerp(color, sand, shore * 0.85f);

    const float snowLine = m_maxHeight - (m_maxHeight - m_minHeight) * 0.12f;
    const float snowBlend = SmoothStep(snowLine, snowLine + 8.0f, height) * SmoothStep(0.45f, 0.2f, slope);
    color = Lerp(color, snow, snowBlend);

    return color * variation;
}

bool Terrain::BuildRenderResources() {
    if (m_heights.empty()) {
        DE_ERROR("Terrain::BuildRenderResources called before Generate().");
        return false;
    }

    const TerrainSettings settings = m_settings;
    auto heightFn = [this](float x, float z) { return HeightAt(x, z); };
    auto colorFn = [this](float x, float z) {
        return MaterialColor(x, z, HeightAt(x, z), NormalAt(x, z));
    };

    render::MeshData data = render::geometry::MakeTerrainGrid(settings.resolution, settings.size,
                                                              heightFn, colorFn);
    if (!m_mesh.Create(data)) {
        DE_ERROR("Failed to create terrain mesh.");
        return false;
    }

    if (!m_heightTexture.CreateR32F(m_samples, m_samples, m_heights, false)) {
        DE_ERROR("Failed to create terrain height texture.");
        return false;
    }

    DE_INFO("Terrain GPU resources built: %zu vertices, %zu triangles.",
            m_mesh.VertexCount(), m_mesh.TriangleCount());
    return true;
}

void Terrain::ReleaseRenderResources() {
    m_mesh.Destroy();
    m_heightTexture.Destroy();
}

} // namespace derecho::scene
