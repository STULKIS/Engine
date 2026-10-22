// Frontier Engine - original procedural geometry.
//
// Everything visible in the world is built from these primitives at load time:
// no external meshes are shipped or fetched.
#pragma once

#include "render/Mesh.h"

#include <functional>

namespace frontier::render::geometry {

// Heightfield grid. `height(x,z)` receives local coordinates in metres.
MeshData MakeTerrainGrid(int resolution, float size,
                         const std::function<float(float, float)>& height,
                         const std::function<vec3(float, float)>& color);

// Simple unit cube (scaled by the caller's transform).
MeshData MakeBox(const vec3& halfExtents, const vec4& color = vec4(1.0f));

// Low-poly boulder: deformed icosphere with noise-driven displacement.
MeshData MakeRock(uint32_t seed, int subdivisions, float roughness);

// Conifer tree with a tapered trunk and layered needle skirts. `lod` 0 = full
// detail, 1 = medium (fewer skirt layers), 2 = cross-card billboard.
MeshData MakeConiferTree(uint32_t seed, int lod);

// Broadleaf tree (trunk + branch cards), used to diversify the forest.
MeshData MakeBroadleafTree(uint32_t seed, int lod);

// Grass clump made of thin tapered blades with vertex tint and wind weights.
MeshData MakeGrassClump(uint32_t seed, int bladeCount, float radius, float height);

// Small fern/shrub for ground cover.
MeshData MakeShrub(uint32_t seed);

// Crossed quads billboard (used by far LOD foliage).
MeshData MakeCrossCard(float width, float height, const vec4& bottomColor, const vec4& topColor);

// Fullscreen triangle for post processing / sky.
MeshData MakeFullscreenTriangle();

// Analytic cylinder between two points.
MeshData MakeCylinder(const vec3& base, const vec3& tip, float baseRadius, float tipRadius,
                      int segments, const vec4& color);

} // namespace frontier::render::geometry
