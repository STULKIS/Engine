#include "render/GeometryBuilder.h"

#include "core/Noise.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace frontier::render::geometry {
namespace {

inline void AddTriangle(MeshData& mesh, uint32_t a, uint32_t b, uint32_t c) {
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
}

inline void AddQuad(MeshData& mesh, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    AddTriangle(mesh, a, b, c);
    AddTriangle(mesh, a, c, d);
}

inline vec3 SafeNormalize(const vec3& v, const vec3& fallback = vec3(0, 1, 0)) {
    const float len = glm::length(v);
    return len > 1e-6f ? v / len : fallback;
}

// Icosphere generation used as the base for rocks and shrub canopies.
struct IcoSphere {
    std::vector<vec3> positions;
    std::vector<uint32_t> indices;

    void Subdivide() {
        std::vector<uint32_t> newIndices;
        newIndices.reserve(indices.size() * 4);
        for (size_t i = 0; i < indices.size(); i += 3) {
            const uint32_t i0 = indices[i];
            const uint32_t i1 = indices[i + 1];
            const uint32_t i2 = indices[i + 2];
            const uint32_t a = Midpoint(i0, i1);
            const uint32_t b = Midpoint(i1, i2);
            const uint32_t c = Midpoint(i2, i0);
            newIndices.insert(newIndices.end(), {i0, a, c, i1, b, a, i2, c, b, a, b, c});
        }
        indices.swap(newIndices);
    }

    uint32_t Midpoint(uint32_t a, uint32_t b) {
        const vec3 mid = SafeNormalize(positions[a] + positions[b]);
        for (uint32_t i = 0; i < positions.size(); ++i) {
            if (glm::dot(positions[i], mid) > 0.99999f) {
                return i;
            }
        }
        positions.push_back(mid);
        return static_cast<uint32_t>(positions.size() - 1);
    }
};

IcoSphere MakeIcoSphereBase() {
    IcoSphere sphere;
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    sphere.positions = {
        {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
        {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
        {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1},
    };
    for (vec3& p : sphere.positions) {
        p = SafeNormalize(p);
    }
    sphere.indices = {
        0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11,
        1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
        3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9,
        4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1,
    };
    return sphere;
}

} // namespace

MeshData MakeTerrainGrid(int resolution, float size, const std::function<float(float, float)>& height,
                         const std::function<vec3(float, float)>& color) {
    MeshData mesh;
    resolution = std::max(2, resolution);
    const int vertsPerSide = resolution + 1;
    mesh.vertices.reserve(static_cast<size_t>(vertsPerSide) * vertsPerSide);
    mesh.indices.reserve(static_cast<size_t>(resolution) * resolution * 6);

    const float half = size * 0.5f;
    const float step = size / static_cast<float>(resolution);
    const float uvScale = size / 12.0f;

    for (int z = 0; z <= resolution; ++z) {
        for (int x = 0; x <= resolution; ++x) {
            const float localX = static_cast<float>(x) * step;
            const float localZ = static_cast<float>(z) * step;
            const float worldX = localX - half;
            const float worldZ = localZ - half;

            Vertex v;
            v.position = vec3(worldX, height(worldX, worldZ), worldZ);
            const float hL = height(worldX - step, worldZ);
            const float hR = height(worldX + step, worldZ);
            const float hD = height(worldX, worldZ - step);
            const float hU = height(worldX, worldZ + step);
            v.normal = SafeNormalize(vec3(hL - hR, 2.0f * step, hD - hU));
            v.uv = vec2(localX, localZ) / uvScale;
            const vec3 tint = color ? color(worldX, worldZ) : vec3(1.0f);
            v.color = vec4(tint, 1.0f);
            mesh.vertices.push_back(v);
        }
    }

    for (int z = 0; z < resolution; ++z) {
        for (int x = 0; x < resolution; ++x) {
            const uint32_t i0 = static_cast<uint32_t>(z * vertsPerSide + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + static_cast<uint32_t>(vertsPerSide);
            const uint32_t i3 = i2 + 1;
            AddQuad(mesh, i0, i2, i3, i1);
        }
    }

    mesh.RecomputeBounds();
    return mesh;
}

MeshData MakeBox(const vec3& halfExtents, const vec4& color) {
    MeshData mesh;
    const vec3 h = halfExtents;
    const std::array<vec3, 24> corners = {
        vec3(-h.x, -h.y, h.z), vec3(h.x, -h.y, h.z), vec3(h.x, h.y, h.z), vec3(-h.x, h.y, h.z),
        vec3(h.x, -h.y, -h.z), vec3(-h.x, -h.y, -h.z), vec3(-h.x, h.y, -h.z), vec3(h.x, h.y, -h.z),
        vec3(-h.x, h.y, h.z), vec3(h.x, h.y, h.z), vec3(h.x, h.y, -h.z), vec3(-h.x, h.y, -h.z),
        vec3(-h.x, -h.y, -h.z), vec3(h.x, -h.y, -h.z), vec3(h.x, -h.y, h.z), vec3(-h.x, -h.y, h.z),
        vec3(h.x, -h.y, h.z), vec3(h.x, -h.y, -h.z), vec3(h.x, h.y, -h.z), vec3(h.x, h.y, h.z),
        vec3(-h.x, -h.y, -h.z), vec3(-h.x, -h.y, h.z), vec3(-h.x, h.y, h.z), vec3(-h.x, h.y, -h.z),
    };
    const std::array<vec3, 6> normals = {
        vec3(0, 0, 1), vec3(0, 0, -1), vec3(0, 1, 0), vec3(0, -1, 0), vec3(1, 0, 0), vec3(-1, 0, 0),
    };
    const std::array<vec2, 4> uvs = {vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1)};

    for (int face = 0; face < 6; ++face) {
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (int corner = 0; corner < 4; ++corner) {
            Vertex v;
            v.position = corners[static_cast<size_t>(face) * 4 + static_cast<size_t>(corner)];
            v.normal = normals[static_cast<size_t>(face)];
            v.uv = uvs[static_cast<size_t>(corner)];
            v.color = color;
            mesh.vertices.push_back(v);
        }
        AddQuad(mesh, base, base + 1, base + 2, base + 3);
    }
    mesh.RecomputeBounds();
    return mesh;
}

MeshData MakeRock(uint32_t seed, int subdivisions, float roughness) {
    IcoSphere sphere = MakeIcoSphereBase();
    for (int i = 0; i < subdivisions; ++i) {
        sphere.Subdivide();
    }

    core::ValueNoise noise(seed * 7919u + 13u);
    core::FractalNoise fractal(seed + 101u, 4, 2.3f, 0.5f);

    MeshData mesh;
    const vec3 squash(1.0f, 0.72f, 0.9f);
    for (const vec3& p : sphere.positions) {
        const float n = noise.Sample3D(p.x * 2.2f, p.y * 2.2f, p.z * 2.2f) * 0.55f +
                        fractal.FBM(p.x * 2.6f, p.z * 2.6f) * 0.45f;
        const float radius = 1.0f + n * roughness * 0.45f;
        Vertex v;
        v.position = p * radius * squash;
        // Push the bottom slightly down so rocks sit convincingly on slopes.
        if (v.position.y < -0.25f) {
            v.position.y *= 0.85f;
        }
        v.normal = SafeNormalize(p / glm::max(squash, vec3(0.001f)));
        v.uv = vec2(p.x * 0.5f + 0.5f, p.z * 0.5f + 0.5f);
        const float tone = 0.85f + n * 0.25f;
        v.color = vec4(tone, tone * 0.97f, tone * 0.93f, 1.0f);
        mesh.vertices.push_back(v);
    }
    mesh.indices = sphere.indices;

    // Recompute smooth normals from displaced positions for a chunkier silhouette.
    for (Vertex& v : mesh.vertices) {
        v.normal = vec3(0.0f);
    }
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        const uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        const vec3 n = glm::cross(mesh.vertices[b].position - mesh.vertices[a].position,
                                  mesh.vertices[c].position - mesh.vertices[a].position);
        mesh.vertices[a].normal += n;
        mesh.vertices[b].normal += n;
        mesh.vertices[c].normal += n;
    }
    for (Vertex& v : mesh.vertices) {
        v.normal = SafeNormalize(v.normal);
    }

    mesh.RecomputeBounds();
    return mesh;
}

MeshData MakeCylinder(const vec3& base, const vec3& tip, float baseRadius, float tipRadius,
                      int segments, const vec4& color) {
    MeshData mesh;
    segments = std::max(3, segments);
    const vec3 axis = tip - base;
    const vec3 dir = SafeNormalize(axis);
    const vec3 reference = std::fabs(dir.y) > 0.95f ? vec3(1, 0, 0) : vec3(0, 1, 0);
    const vec3 tangent = SafeNormalize(glm::cross(reference, dir));
    const vec3 bitangent = glm::cross(dir, tangent);

    for (int i = 0; i <= segments; ++i) {
        const float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
        const vec3 offset = tangent * std::cos(angle) + bitangent * std::sin(angle);
        Vertex vb;
        vb.position = base + offset * baseRadius;
        vb.normal = offset;
        vb.uv = vec2(static_cast<float>(i) / segments, 0.0f);
        vb.color = color;
        mesh.vertices.push_back(vb);

        Vertex vt;
        vt.position = tip + offset * tipRadius;
        vt.normal = offset;
        vt.uv = vec2(static_cast<float>(i) / segments, 1.0f);
        vt.color = color;
        mesh.vertices.push_back(vt);
    }
    for (int i = 0; i < segments; ++i) {
        const uint32_t b0 = static_cast<uint32_t>(i * 2);
        const uint32_t t0 = b0 + 1;
        const uint32_t b1 = b0 + 2;
        const uint32_t t1 = b0 + 3;
        AddQuad(mesh, b0, b1, t1, t0);
    }
    mesh.RecomputeBounds();
    return mesh;
}

MeshData MakeConiferTree(uint32_t seed, int lod) {
    core::Random rng(seed);
    MeshData mesh;

    const float height = lod == 2 ? 1.0f : rng.Range(7.0f, 12.5f);
    const float trunkRadius = height * rng.Range(0.022f, 0.033f);
    const vec4 bark(0.28f, 0.21f, 0.15f, 0.0f);

    if (lod == 2) {
        return MakeCrossCard(3.4f, 12.0f, vec4(0.08f, 0.14f, 0.06f, 0.0f), vec4(0.2f, 0.33f, 0.14f, 0.6f));
    }

    MeshData trunk = MakeCylinder(vec3(0, 0, 0), vec3(0, height * 0.92f, 0), trunkRadius,
                                  trunkRadius * 0.42f, lod == 0 ? 9 : 6, bark);
    mesh.vertices = trunk.vertices;
    mesh.indices = trunk.indices;

    const int layers = lod == 0 ? 6 : 3;
    const vec3 needleBase(0.11f, 0.2f, 0.1f);
    for (int layer = 0; layer < layers; ++layer) {
        const float t = static_cast<float>(layer) / static_cast<float>(std::max(1, layers - 1));
        const float y = height * (0.22f + t * 0.72f);
        const float layerRadius = height * 0.26f * (1.0f - t * 0.85f) * rng.Range(0.9f, 1.1f);
        const float layerHeight = height * (0.3f - t * 0.1f);
        const int segments = lod == 0 ? 10 : 7;

        const uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
        for (int i = 0; i <= segments; ++i) {
            const float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
            Vertex rim;
            rim.position = vec3(std::cos(angle) * layerRadius, y, std::sin(angle) * layerRadius);
            const float variation = core::Cellular2D(std::cos(angle) * 2.0f, std::sin(angle) * 2.0f, seed + layer);
            rim.normal = SafeNormalize(vec3(std::cos(angle), 0.65f, std::sin(angle)));
            rim.uv = vec2(static_cast<float>(i) / segments * 2.0f, t);
            rim.color = vec4(needleBase * (0.8f + variation * 0.35f), 0.15f);
            mesh.vertices.push_back(rim);

            Vertex apex;
            apex.position = vec3(0, y + layerHeight, 0);
            apex.normal = SafeNormalize(vec3(std::cos(angle), 1.4f, std::sin(angle)));
            apex.uv = vec2(static_cast<float>(i) / segments * 2.0f, t + 0.1f);
            apex.color = vec4(needleBase * 1.05f, 0.7f);
            mesh.vertices.push_back(apex);
        }
        for (int i = 0; i < segments; ++i) {
            const uint32_t r0 = start + static_cast<uint32_t>(i * 2);
            const uint32_t a0 = r0 + 1;
            const uint32_t r1 = r0 + 2;
            const uint32_t a1 = r0 + 3;
            AddTriangle(mesh, r0, r1, a1);
            AddTriangle(mesh, r0, a1, a0);
        }
    }

    mesh.RecomputeBounds();
    return mesh;
}

MeshData MakeBroadleafTree(uint32_t seed, int lod) {
    core::Random rng(seed);
    const float height = lod == 2 ? 1.0f : rng.Range(6.0f, 10.0f);
    if (lod == 2) {
        return MakeCrossCard(4.6f, 9.0f, vec4(0.09f, 0.16f, 0.06f, 0.0f), vec4(0.24f, 0.36f, 0.13f, 0.6f));
    }

    const vec4 bark(0.3f, 0.23f, 0.17f, 0.0f);
    MeshData mesh = MakeCylinder(vec3(0, 0, 0), vec3(0, height * 0.7f, 0), height * 0.045f,
                                 height * 0.028f, lod == 0 ? 8 : 6, bark);

    const int branches = lod == 0 ? 5 : 3;
    for (int i = 0; i < branches; ++i) {
        const float angle = (kTwoPi / branches) * static_cast<float>(i) + rng.Range(-0.3f, 0.3f);
        const float y = height * rng.Range(0.4f, 0.68f);
        const float len = height * rng.Range(0.22f, 0.34f);
        const vec3 base(0, y, 0);
        const vec3 tip(std::cos(angle) * len, y + len * 0.65f, std::sin(angle) * len);
        MeshData branch = MakeCylinder(base, tip, height * 0.018f, height * 0.008f, 5, bark);
        const uint32_t offset = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.insert(mesh.vertices.end(), branch.vertices.begin(), branch.vertices.end());
        for (uint32_t index : branch.indices) {
            mesh.indices.push_back(index + offset);
        }
    }

    // Canopy: a cluster of low-poly blobs with foliage tint.
    const int blobs = lod == 0 ? 7 : 4;
    for (int i = 0; i < blobs; ++i) {
        const float angle = rng.Range(0.0f, kTwoPi);
        const float radius = height * rng.Range(0.12f, 0.3f);
        const float cx = std::cos(angle) * height * rng.Range(0.05f, 0.22f);
        const float cz = std::sin(angle) * height * rng.Range(0.05f, 0.22f);
        const float cy = height * rng.Range(0.62f, 0.92f);

        MeshData blob = MakeRock(seed + static_cast<uint32_t>(i) * 17u, lod == 0 ? 1 : 0, 0.6f);
        const uint32_t offset = static_cast<uint32_t>(mesh.vertices.size());
        const vec4 leafTint(rng.Range(0.7f, 1.05f), rng.Range(0.85f, 1.1f), rng.Range(0.6f, 0.9f), 0.55f);
        for (Vertex v : blob.vertices) {
            v.position = v.position * radius + vec3(cx, cy, cz);
            v.color = vec4(0.17f, 0.28f, 0.1f, 0.55f) * leafTint;
            v.color.a = 0.55f;
            mesh.vertices.push_back(v);
        }
        for (uint32_t index : blob.indices) {
            mesh.indices.push_back(index + offset);
        }
    }

    mesh.RecomputeBounds();
    return mesh;
}

MeshData MakeGrassClump(uint32_t seed, int bladeCount, float radius, float height) {
    core::Random rng(seed);
    MeshData mesh;
    bladeCount = std::max(1, bladeCount);

    for (int blade = 0; blade < bladeCount; ++blade) {
        const float angle = rng.Range(0.0f, kTwoPi);
        const float offsetRadius = rng.Range(0.0f, radius);
        const vec3 base(std::cos(angle) * offsetRadius, 0.0f, std::sin(angle) * offsetRadius);
        const float bladeHeight = height * rng.Range(0.55f, 1.15f);
        const float width = rng.Range(0.025f, 0.055f);
        const float lean = rng.Range(-0.25f, 0.25f);

        const vec3 side(std::cos(angle + kPi * 0.5f), 0.0f, std::sin(angle + kPi * 0.5f));
        const vec3 tip = base + vec3(lean * bladeHeight, bladeHeight, lean * bladeHeight * 0.6f);

        const vec4 baseColor(0.11f, 0.2f, 0.06f, 0.0f);
        const vec4 tipColor(0.38f, 0.55f, 0.17f, 1.0f);

        const uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
        Vertex v0, v1, v2;
        v0.position = base - side * width;
        v0.normal = vec3(0, 1, 0);
        v0.uv = vec2(0.0f, 0.0f);
        v0.color = baseColor;

        v1.position = base + side * width;
        v1.normal = vec3(0, 1, 0);
        v1.uv = vec2(1.0f, 0.0f);
        v1.color = baseColor;

        v2.position = tip;
        v2.normal = vec3(0, 1, 0);
        v2.uv = vec2(0.5f, 1.0f);
        v2.color = tipColor;

        mesh.vertices.push_back(v0);
        mesh.vertices.push_back(v1);
        mesh.vertices.push_back(v2);
        AddTriangle(mesh, start, start + 1, start + 2);
    }

    mesh.RecomputeBounds();
    return mesh;
}

MeshData MakeShrub(uint32_t seed) {
    core::Random rng(seed);
    MeshData mesh;
    const int blobs = rng.RangeInt(2, 4);
    for (int i = 0; i < blobs; ++i) {
        MeshData blob = MakeRock(seed + static_cast<uint32_t>(i) * 31u, 0, 0.55f);
        const float scale = rng.Range(0.35f, 0.7f);
        const vec3 center(rng.Range(-0.5f, 0.5f), rng.Range(0.25f, 0.6f), rng.Range(-0.5f, 0.5f));
        const uint32_t offset = static_cast<uint32_t>(mesh.vertices.size());
        for (Vertex v : blob.vertices) {
            v.position = v.position * scale + center;
            v.color = vec4(rng.Range(0.12f, 0.2f), rng.Range(0.22f, 0.34f), rng.Range(0.08f, 0.14f), 0.35f);
            mesh.vertices.push_back(v);
        }
        for (uint32_t index : blob.indices) {
            mesh.indices.push_back(index + offset);
        }
    }
    mesh.RecomputeBounds();
    return mesh;
}

MeshData MakeCrossCard(float width, float height, const vec4& bottomColor, const vec4& topColor) {
    MeshData mesh;
    const vec2 uvs[4] = {vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1)};
    const vec4 colors[4] = {bottomColor, bottomColor, topColor, topColor};
    const float halfW = width * 0.5f;

    for (int plane = 0; plane < 2; ++plane) {
        const vec3 a = plane == 0 ? vec3(-halfW, 0, 0) : vec3(0, 0, -halfW);
        const vec3 b = plane == 0 ? vec3(halfW, 0, 0) : vec3(0, 0, halfW);
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        const vec3 positions[4] = {a, b, b + vec3(0, height, 0), a + vec3(0, height, 0)};
        for (int i = 0; i < 4; ++i) {
            Vertex v;
            v.position = positions[i];
            v.normal = plane == 0 ? vec3(0, 0, 1) : vec3(1, 0, 0);
            v.uv = uvs[i];
            v.color = colors[i];
            mesh.vertices.push_back(v);
        }
        AddQuad(mesh, base, base + 1, base + 2, base + 3);
        AddQuad(mesh, base, base + 2, base + 1, base + 3); // double-sided
    }
    mesh.RecomputeBounds();
    return mesh;
}

MeshData MakeFullscreenTriangle() {
    MeshData mesh;
    Vertex v;
    v.normal = vec3(0, 0, 1);
    v.color = vec4(1.0f);

    v.position = vec3(-1.0f, -1.0f, 0.0f);
    v.uv = vec2(0.0f, 0.0f);
    mesh.vertices.push_back(v);
    v.position = vec3(3.0f, -1.0f, 0.0f);
    v.uv = vec2(2.0f, 0.0f);
    mesh.vertices.push_back(v);
    v.position = vec3(-1.0f, 3.0f, 0.0f);
    v.uv = vec2(0.0f, 2.0f);
    mesh.vertices.push_back(v);

    mesh.indices = {0, 1, 2};
    mesh.bounds = {vec3(-1, -1, 0), vec3(1, 1, 0)};
    return mesh;
}

} // namespace frontier::render::geometry
